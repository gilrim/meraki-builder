#include "websocket.h"
#include "config_apply.h"
#include "config_file.h"
#include "configd.h"
#include "network.h"
#include "result.h"
#include "status.h"
#include "validation.h"

#include <libpostmerkos.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct per_session_data {
  bool send_initial_status;
  bool send_initial_config;
  bool send_status;
  bool send_config;
  char *reply;
  char *receive_buffer;
  size_t receive_length;
};

static struct lws *clients[MAX_CLIENTS];
static size_t client_count;
static bool status_pending;
static bool config_pending;
static char *cached_status_json;
static char *cached_config_json;
static long long config_mtime_ns;
static int configured_status_interval = 3;
static struct lws_sorted_usec_list status_sul;
static struct lws_sorted_usec_list config_sul;
static struct lws_sorted_usec_list network_sul;
static struct lws_context *ws_context;

static bool management_address_changed(const struct network_runtime *before,
                                       const struct network_runtime *after) {
  if (!before || !after) return false;
  return strcmp(before->applied.address, after->applied.address) ||
         before->applied.prefix != after->applied.prefix;
}

static void rebind_management_services(const struct network_runtime *before,
                                       const struct network_runtime *after) {
  if (!before || !after || dry_run || !after->applied.address[0]) return;

  const char *script = getenv("CONFIGD_NETWORK_REBIND");
  if (!script || !*script) script = "/usr/sbin/postmerkos-network-rebind";
  if (access(script, X_OK) != 0) {
    fprintf(stderr, "%s network: rebind hook is unavailable: %s\n",
            get_time(), script);
    return;
  }

  char old_cidr[32];
  char new_cidr[32];
  snprintf(old_cidr, sizeof(old_cidr), "%s/%u",
           before->applied.address[0] ? before->applied.address : "0.0.0.0",
           before->applied.prefix);
  snprintf(new_cidr, sizeof(new_cidr), "%s/%u", after->applied.address,
           after->applied.prefix);

  fprintf(stderr, "%s network: management address changed %s -> %s; "
                  "rebinding web service\n",
          get_time(), old_cidr, new_cidr);
  pid_t child = fork();
  if (child == 0) {
    execl(script, script, old_cidr, new_cidr, after->source, (char *)NULL);
    _exit(127);
  }
  if (child < 0) {
    fprintf(stderr, "%s network: unable to start rebind hook: %s\n",
            get_time(), strerror(errno));
    return;
  }

  int status = 0;
  if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0) {
    fprintf(stderr, "%s network: web-service rebind hook failed\n",
            get_time());
  }
}

char *wrap_message(const char *type, struct json_object *data,
                   struct json_object *request_id) {
  struct json_object *message = json_object_new_object();
  if (request_id)
    json_object_object_add(message, "id", json_object_get(request_id));
  json_object_object_add(message, "type", json_object_new_string(type));
  json_object_object_add(message, "data",
                         data ? json_object_get(data) : json_object_new_null());
  const char *text = json_object_to_json_string_ext(message,
                                                    JSON_C_TO_STRING_PLAIN);
  char *result = strdup(text);
  json_object_put(message);
  return result;
}

static void add_client(struct lws *wsi) {
  if (client_count < MAX_CLIENTS) clients[client_count++] = wsi;
}

static void remove_client(struct lws *wsi) {
  for (size_t i = 0; i < client_count; i++) {
    if (clients[i] == wsi) {
      clients[i] = clients[--client_count];
      return;
    }
  }
}

static void request_writable_all(void) {
  for (size_t i = 0; i < client_count; i++)
    lws_callback_on_writable(clients[i]);
}

static int ws_send(struct lws *wsi, const char *text) {
  if (!text) return -EINVAL;
  size_t length = strlen(text);
  unsigned char *buffer = malloc(LWS_PRE + length);
  if (!buffer) return -ENOMEM;
  memcpy(buffer + LWS_PRE, text, length);
  int written = lws_write(wsi, buffer + LWS_PRE, length, LWS_WRITE_TEXT);
  free(buffer);
  return written == (int)length ? 0 : -EIO;
}

static void refresh_status_cache(bool broadcast) {
  struct json_object *status = get_status();
  char *message = wrap_message("status", status, NULL);
  json_object_put(status);
  if (!message) return;
  if (!cached_status_json || strcmp(cached_status_json, message)) {
    free(cached_status_json);
    cached_status_json = message;
    if (broadcast) {
      status_pending = true;
      request_writable_all();
    }
  } else {
    free(message);
  }
}

static void refresh_config_cache(struct json_object *config, bool broadcast) {
  char *message = wrap_message("config", config, NULL);
  if (!message) return;
  free(cached_config_json);
  cached_config_json = message;
  if (broadcast) {
    config_pending = true;
    request_writable_all();
  }
}

static void status_poll_cb(struct lws_sorted_usec_list *sul) {
  refresh_status_cache(true);
  lws_sul_schedule(ws_context, 0, sul, status_poll_cb,
      (lws_usec_t)configured_status_interval * LWS_USEC_PER_SEC);
}

static void network_poll_cb(struct lws_sorted_usec_list *sul) {
  struct network_runtime before = *network_manager_runtime();
  struct apply_result result;
  apply_result_init(&result);
  bool changed = network_manager_poll(&result);
  const struct network_runtime *after = network_manager_runtime();
  if (json_object_array_length(result.warnings) > 0)
    fprintf(stderr, "%s network: %s\n", get_time(),
            json_object_to_json_string(result.warnings));
  apply_result_cleanup(&result);
  if (management_address_changed(&before, after))
    rebind_management_services(&before, after);
  if (changed) refresh_status_cache(true);
  lws_sul_schedule(ws_context, 0, sul, network_poll_cb,
      (lws_usec_t)network_manager_next_poll_seconds() * LWS_USEC_PER_SEC);
}

static void config_poll_cb(struct lws_sorted_usec_list *sul) {
  long long current_mtime = 0;
  if (config_file_mtime(&current_mtime) == 0 &&
      current_mtime != config_mtime_ns) {
    config_mtime_ns = current_mtime;
    struct json_object *config = load_config_file();
    char error[256];
    if (!config || validate_configuration(config, error, sizeof(error)) != 0) {
      config_file_set_runtime_error(config ? error :
                                    "configuration file could not be parsed");
      if (config) json_object_put(config);
      refresh_status_cache(true);
    } else {
      config_file_set_runtime_error(NULL);
      struct apply_result result;
      apply_result_init(&result);
      struct network_runtime before = *network_manager_runtime();
      config_apply_full(config, &result);
      const struct network_runtime *after = network_manager_runtime();
      if (json_object_array_length(result.warnings) > 0)
        fprintf(stderr, "%s config reload: %s\n", get_time(),
                json_object_to_json_string(result.warnings));
      apply_result_cleanup(&result);
      if (management_address_changed(&before, after))
        rebind_management_services(&before, after);
      refresh_config_cache(config, true);
      refresh_status_cache(true);
      json_object_put(config);
    }
  }
  lws_sul_schedule(ws_context, 0, sul, config_poll_cb,
                   10 * LWS_USEC_PER_SEC);
}

static struct json_object *error_data(const char *detail) {
  struct json_object *data = json_object_new_object();
  json_object_object_add(data, "status", json_object_new_int(400));
  json_object_object_add(data, "message", json_object_new_string("Bad Request"));
  if (detail && *detail)
    json_object_object_add(data, "detail", json_object_new_string(detail));
  return data;
}

static void queue_response(struct lws *wsi, struct per_session_data *session,
                           const char *type, struct json_object *data,
                           struct json_object *request_id) {
  free(session->reply);
  session->reply = wrap_message(type, data, request_id);
  lws_callback_on_writable(wsi);
}

static void queue_bad_request(struct lws *wsi,
                              struct per_session_data *session,
                              struct json_object *request_id,
                              const char *detail) {
  struct json_object *data = error_data(detail);
  queue_response(wsi, session, "error", data, request_id);
  json_object_put(data);
}

static int handle_request(struct lws *wsi, struct per_session_data *session,
                          struct json_object *message) {
  if (!json_object_is_type(message, json_type_object)) {
    queue_bad_request(wsi, session, NULL, "request must be a JSON object");
    return 0;
  }

  struct json_object *request_id = NULL;
  json_object_object_get_ex(message, "id", &request_id);
  struct json_object *type_object = NULL;
  if (!json_object_object_get_ex(message, "type", &type_object) ||
      !json_object_is_type(type_object, json_type_string)) {
    queue_bad_request(wsi, session, request_id, "type must be a string");
    return 0;
  }
  const char *type = json_object_get_string(type_object);

  if (!strcmp(type, "get_status")) {
    struct json_object *status = get_status();
    queue_response(wsi, session, "status", status, request_id);
    json_object_put(status);
    return 0;
  }
  if (!strcmp(type, "get_config")) {
    struct json_object *config = load_config_file();
    if (!config) {
      queue_bad_request(wsi, session, request_id,
                        "persistent configuration is unavailable");
    } else {
      queue_response(wsi, session, "config", config, request_id);
      json_object_put(config);
    }
    return 0;
  }
  if (strcmp(type, "config")) {
    queue_bad_request(wsi, session, request_id, "unknown request type");
    return 0;
  }

  struct json_object *delta = NULL;
  if (!json_object_object_get_ex(message, "data", &delta) ||
      !json_object_is_type(delta, json_type_object)) {
    queue_bad_request(wsi, session, request_id,
                      "config.data must be a JSON object");
    return 0;
  }

  struct apply_result result;
  apply_result_init(&result);
  struct json_object *saved = NULL;
  char error[256];
  int rc = config_merge_validate_save_apply(delta, &saved, &result, true,
                                             error, sizeof(error));
  if (rc != 0) {
    queue_bad_request(wsi, session, request_id, error);
    apply_result_cleanup(&result);
    return 0;
  }

  struct json_object *ack = apply_result_json(&result,
                                              "Configuration accepted");
  queue_response(wsi, session, "ack", ack, request_id);
  json_object_put(ack);
  apply_result_cleanup(&result);

  config_file_mtime(&config_mtime_ns);
  refresh_config_cache(saved, true);
  refresh_status_cache(true);
  lws_sul_schedule(ws_context, 0, &network_sul, network_poll_cb,
                   1 * LWS_USEC_PER_SEC);
  json_object_put(saved);
  return 0;
}

static void reset_receive(struct per_session_data *session) {
  free(session->receive_buffer);
  session->receive_buffer = NULL;
  session->receive_length = 0;
}

static int receive_fragment(struct lws *wsi, struct per_session_data *session,
                            const void *input, size_t length) {
  if (lws_frame_is_binary(wsi)) {
    queue_bad_request(wsi, session, NULL, "binary frames are not supported");
    reset_receive(session);
    return 0;
  }
  if (session->receive_length + length > MAX_MSG_LEN) {
    queue_bad_request(wsi, session, NULL, "request exceeds maximum size");
    reset_receive(session);
    return 0;
  }
  char *expanded = realloc(session->receive_buffer,
                           session->receive_length + length + 1);
  if (!expanded) {
    queue_bad_request(wsi, session, NULL, "request buffer allocation failed");
    reset_receive(session);
    return 0;
  }
  session->receive_buffer = expanded;
  memcpy(session->receive_buffer + session->receive_length, input, length);
  session->receive_length += length;
  session->receive_buffer[session->receive_length] = '\0';

  if (!lws_is_final_fragment(wsi) || lws_remaining_packet_payload(wsi) != 0)
    return 0;

  struct json_tokener *tokener = json_tokener_new();
  if (!tokener) {
    queue_bad_request(wsi, session, NULL, "JSON parser allocation failed");
    reset_receive(session);
    return 0;
  }
  struct json_object *message = json_tokener_parse_ex(
      tokener, session->receive_buffer, (int)session->receive_length);
  enum json_tokener_error parse_error = json_tokener_get_error(tokener);
  if (parse_error != json_tokener_success || !message) {
    queue_bad_request(wsi, session, NULL, "malformed JSON");
  } else {
    handle_request(wsi, session, message);
    json_object_put(message);
  }
  json_tokener_free(tokener);
  reset_receive(session);
  return 0;
}

static int configd_ws_callback(struct lws *wsi,
                               enum lws_callback_reasons reason,
                               void *user, void *input, size_t length) {
  struct per_session_data *session = user;
  switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
      if (client_count >= MAX_CLIENTS) {
        queue_bad_request(wsi, session, NULL, "server client limit reached");
        return -1;
      }
      add_client(wsi);
      session->send_initial_status = true;
      session->send_initial_config = true;
      lws_callback_on_writable(wsi);
      printf("ws: client connected (%zu total)\n", client_count);
      break;

    case LWS_CALLBACK_CLOSED:
      remove_client(wsi);
      reset_receive(session);
      free(session->reply);
      session->reply = NULL;
      printf("ws: client disconnected (%zu total)\n", client_count);
      break;

    case LWS_CALLBACK_RECEIVE:
      receive_fragment(wsi, session, input, length);
      break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
      if (session->reply) {
        ws_send(wsi, session->reply);
        free(session->reply);
        session->reply = NULL;
      } else if (session->send_initial_status && cached_status_json) {
        ws_send(wsi, cached_status_json);
        session->send_initial_status = false;
      } else if (session->send_initial_config && cached_config_json) {
        ws_send(wsi, cached_config_json);
        session->send_initial_config = false;
      } else if (session->send_status && cached_status_json) {
        ws_send(wsi, cached_status_json);
        session->send_status = false;
      } else if (session->send_config && cached_config_json) {
        ws_send(wsi, cached_config_json);
        session->send_config = false;
      }
      if (session->reply || session->send_initial_status ||
          session->send_initial_config || session->send_status ||
          session->send_config)
        lws_callback_on_writable(wsi);
      break;

    default:
      break;
  }
  return 0;
}

void mark_clients_pending(void) {
  if (!status_pending && !config_pending) return;
  for (size_t i = 0; i < client_count; i++) {
    struct per_session_data *session = lws_wsi_user(clients[i]);
    if (!session) continue;
    if (status_pending) session->send_status = true;
    if (config_pending) session->send_config = true;
    lws_callback_on_writable(clients[i]);
  }
  status_pending = false;
  config_pending = false;
}

static const struct lws_protocols protocols[] = {
  {"configd-ws", configd_ws_callback, sizeof(struct per_session_data),
   MAX_MSG_LEN},
  LWS_PROTOCOL_LIST_TERM
};

struct lws_context *ws_init(int port) {
  struct lws_context_creation_info info;
  memset(&info, 0, sizeof(info));
  info.port = port;
  info.protocols = protocols;
  info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE |
                 LWS_SERVER_OPTION_ALLOW_LISTEN_SHARE;
  lws_set_log_level(LLL_ERR | LLL_WARN, NULL);
  ws_context = lws_create_context(&info);
  return ws_context;
}

void ws_schedule_timers(struct lws_context *context, int status_interval) {
  configured_status_interval = status_interval;
  config_file_mtime(&config_mtime_ns);
  refresh_status_cache(false);
  struct json_object *config = load_config_file();
  if (config) {
    refresh_config_cache(config, false);
    json_object_put(config);
  }
  lws_sul_schedule(context, 0, &status_sul, status_poll_cb,
      (lws_usec_t)configured_status_interval * LWS_USEC_PER_SEC);
  lws_sul_schedule(context, 0, &config_sul, config_poll_cb,
      10 * LWS_USEC_PER_SEC);
  lws_sul_schedule(context, 0, &network_sul, network_poll_cb,
      (lws_usec_t)network_manager_next_poll_seconds() * LWS_USEC_PER_SEC);
}

void ws_shutdown(struct lws_context *context) {
  lws_sul_cancel(&status_sul);
  lws_sul_cancel(&config_sul);
  lws_sul_cancel(&network_sul);
  free(cached_status_json);
  free(cached_config_json);
  cached_status_json = NULL;
  cached_config_json = NULL;
  lws_context_destroy(context);
  ws_context = NULL;
}
