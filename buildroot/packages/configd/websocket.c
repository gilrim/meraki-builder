#include "websocket.h"
#include "auth.h"
#include "config_apply.h"
#include "config_file.h"
#include "configd.h"
#include "network.h"
#include "result.h"
#include "status.h"
#include "system_ops.h"
#include "validation.h"

#include <libpostmerkos.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_FIRMWARE_UPLOAD (16U * 1024U * 1024U)

struct queued_reply {
  char *text;
  struct queued_reply *next;
};

struct per_session_data {
  bool authenticated;
  char username[65];
  unsigned int auth_failures;
  bool send_initial_status;
  bool send_initial_config;
  bool send_status;
  bool send_config;
  struct queued_reply *reply_head;
  struct queued_reply *reply_tail;
  char *receive_buffer;
  size_t receive_length;

  bool upload_active;
  bool upload_handed_off;
  int upload_fd;
  char upload_path[256];
  char upload_name[128];
  char upload_sha256[65];
  char upload_overlay[16];
  size_t upload_expected;
  size_t upload_received;
  bool upload_force;
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
static struct lws *upload_owner;

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
    char error[256] = {0};
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

static struct json_object *error_data_status(int status, const char *message,
                                             const char *detail) {
  struct json_object *data = json_object_new_object();
  json_object_object_add(data, "status", json_object_new_int(status));
  json_object_object_add(data, "message",
                         json_object_new_string(message ? message : "Error"));
  if (detail && *detail)
    json_object_object_add(data, "detail", json_object_new_string(detail));
  return data;
}

static void queue_response(struct lws *wsi, struct per_session_data *session,
                           const char *type, struct json_object *data,
                           struct json_object *request_id) {
  struct queued_reply *reply = calloc(1, sizeof(*reply));
  if (!reply) return;
  reply->text = wrap_message(type, data, request_id);
  if (!reply->text) {
    free(reply);
    return;
  }
  if (session->reply_tail) session->reply_tail->next = reply;
  else session->reply_head = reply;
  session->reply_tail = reply;
  lws_callback_on_writable(wsi);
}

static void free_replies(struct per_session_data *session) {
  while (session->reply_head) {
    struct queued_reply *reply = session->reply_head;
    session->reply_head = reply->next;
    free(reply->text);
    free(reply);
  }
  session->reply_tail = NULL;
}

static void queue_error(struct lws *wsi, struct per_session_data *session,
                        struct json_object *request_id, int status,
                        const char *message, const char *detail) {
  struct json_object *data = error_data_status(status, message, detail);
  queue_response(wsi, session, "error", data, request_id);
  json_object_put(data);
}

static void queue_bad_request(struct lws *wsi,
                              struct per_session_data *session,
                              struct json_object *request_id,
                              const char *detail) {
  queue_error(wsi, session, request_id, 400, "Bad Request", detail);
}

static struct json_object *request_data_object(struct json_object *message) {
  struct json_object *data = NULL;
  if (!json_object_object_get_ex(message, "data", &data) ||
      !json_object_is_type(data, json_type_object))
    return NULL;
  return data;
}

static const char *object_string(struct json_object *object, const char *key) {
  struct json_object *value = NULL;
  if (!object || !json_object_object_get_ex(object, key, &value) ||
      !json_object_is_type(value, json_type_string))
    return NULL;
  return json_object_get_string(value);
}

static bool valid_sha256(const char *value) {
  if (!value || strlen(value) != 64) return false;
  for (const unsigned char *p = (const unsigned char *)value; *p; p++)
    if (!isxdigit(*p)) return false;
  return true;
}

static bool valid_overlay(const char *value) {
  return value && (!strcmp(value, "preserve") || !strcmp(value, "migrate") ||
                   !strcmp(value, "reset") || !strcmp(value, "image"));
}

static void cleanup_upload(struct lws *wsi, struct per_session_data *session) {
  if (session->upload_fd >= 0) close(session->upload_fd);
  session->upload_fd = -1;
  if (session->upload_path[0] && !session->upload_handed_off)
    unlink(session->upload_path);
  session->upload_active = false;
  session->upload_expected = 0;
  session->upload_received = 0;
  session->upload_path[0] = '\0';
  if (upload_owner == wsi) upload_owner = NULL;
}

static struct json_object *upload_status_json(
    const struct per_session_data *session) {
  struct json_object *data = json_object_new_object();
  json_object_object_add(data, "active",
                         json_object_new_boolean(session->upload_active));
  json_object_object_add(data, "name",
                         json_object_new_string(session->upload_name));
  json_object_object_add(data, "received",
                         json_object_new_int64((int64_t)session->upload_received));
  json_object_object_add(data, "expected",
                         json_object_new_int64((int64_t)session->upload_expected));
  int progress = session->upload_expected
      ? (int)((session->upload_received * 100U) / session->upload_expected) : 0;
  json_object_object_add(data, "progress", json_object_new_int(progress));
  return data;
}

static int replace_configuration(struct json_object *candidate,
                                 struct apply_result *result,
                                 char *error, size_t error_size) {
  int rc = validate_configuration(candidate, error, error_size);
  if (rc != 0) return rc;
  struct network_runtime before = *network_manager_runtime();
  rc = save_config_file(candidate, error, error_size);
  if (rc != 0) return rc;
  rc = config_apply_full(candidate, result);
  const struct network_runtime *after = network_manager_runtime();
  if (management_address_changed(&before, after))
    rebind_management_services(&before, after);
  return rc;
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

  if (!strcmp(type, "auth")) {
    struct json_object *data = request_data_object(message);
    const char *username = object_string(data, "username");
    const char *password = object_string(data, "password");
    char error[256] = {0};
    if (!data || auth_verify_user(username, password, error, sizeof(error)) != 0) {
      session->authenticated = false;
      session->username[0] = '\0';
      session->auth_failures++;
      if (session->auth_failures > 2) usleep(500000);
      queue_error(wsi, session, request_id, 401, "Unauthorized", error);
      return 0;
    }
    session->authenticated = true;
    session->auth_failures = 0;
    snprintf(session->username, sizeof(session->username), "%s", username);
    session->send_initial_status = true;
    session->send_initial_config = true;
    struct json_object *auth = json_object_new_object();
    json_object_object_add(auth, "username", json_object_new_string(username));
    json_object_object_add(auth, "users", auth_list_users());
    queue_response(wsi, session, "auth", auth, request_id);
    json_object_put(auth);
    return 0;
  }

  if (!strcmp(type, "logout")) {
    session->authenticated = false;
    session->username[0] = '\0';
    session->send_initial_status = false;
    session->send_initial_config = false;
    cleanup_upload(wsi, session);
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "message", json_object_new_string("Logged out"));
    queue_response(wsi, session, "auth_required", data, request_id);
    json_object_put(data);
    return 0;
  }

  if (!session->authenticated) {
    queue_error(wsi, session, request_id, 401, "Unauthorized",
                "authenticate with Linux PAM before using this interface");
    return 0;
  }

  if (!strcmp(type, "get_auth")) {
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "username",
                           json_object_new_string(session->username));
    json_object_object_add(data, "users", auth_list_users());
    queue_response(wsi, session, "auth", data, request_id);
    json_object_put(data);
    return 0;
  }

  if (!strcmp(type, "user_list")) {
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "users", auth_list_users());
    queue_response(wsi, session, "users", data, request_id);
    json_object_put(data);
    return 0;
  }

  if (!strcmp(type, "password_change")) {
    struct json_object *data = request_data_object(message);
    const char *target = object_string(data, "username");
    const char *current = object_string(data, "current_password");
    const char *replacement = object_string(data, "new_password");
    if (!target || !*target) target = session->username;
    char error[256] = {0};
    if (!data || auth_change_password(session->username, target, current,
                                      replacement, error, sizeof(error)) != 0) {
      queue_error(wsi, session, request_id, 400, "Password update failed",
                  error);
      return 0;
    }
    struct json_object *ack = json_object_new_object();
    json_object_object_add(ack, "message",
                           json_object_new_string("Password updated"));
    json_object_object_add(ack, "username", json_object_new_string(target));
    queue_response(wsi, session, "ack", ack, request_id);
    json_object_put(ack);
    return 0;
  }

  if (!strcmp(type, "terminal_exec")) {
    struct json_object *data = request_data_object(message);
    const char *command = object_string(data, "command");
    if (!command) {
      queue_bad_request(wsi, session, request_id,
                        "terminal_exec.data.command must be a string");
      return 0;
    }
    struct json_object *result = terminal_execute(command);
    queue_response(wsi, session, "terminal", result, request_id);
    json_object_put(result);
    return 0;
  }

  if (!strcmp(type, "firmware_status")) {
    struct json_object *status = firmware_status_json();
    queue_response(wsi, session, "firmware_status", status, request_id);
    json_object_put(status);
    return 0;
  }

  if (!strcmp(type, "firmware_upload_status")) {
    struct json_object *data = upload_status_json(session);
    queue_response(wsi, session, "firmware_upload_status", data, request_id);
    json_object_put(data);
    return 0;
  }

  if (!strcmp(type, "firmware_upload_cancel")) {
    cleanup_upload(wsi, session);
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "message",
                           json_object_new_string("Firmware upload cancelled"));
    queue_response(wsi, session, "ack", data, request_id);
    json_object_put(data);
    return 0;
  }

  if (!strcmp(type, "firmware_upload_start")) {
    struct json_object *data = request_data_object(message);
    const char *name = object_string(data, "name");
    const char *sha256 = object_string(data, "sha256");
    const char *overlay = object_string(data, "overlay");
    struct json_object *size_object = NULL;
    struct json_object *force_object = NULL;
    if (!data || !json_object_object_get_ex(data, "size", &size_object)) {
      queue_bad_request(wsi, session, request_id, "firmware size is required");
      return 0;
    }
    int64_t size_value = json_object_get_int64(size_object);
    bool force = json_object_object_get_ex(data, "force", &force_object) &&
                 json_object_get_boolean(force_object);
    if (size_value <= 0 || size_value > MAX_FIRMWARE_UPLOAD ||
        !valid_sha256(sha256) || !valid_overlay(overlay)) {
      queue_bad_request(wsi, session, request_id,
                        "invalid firmware size, SHA-256, or overlay policy");
      return 0;
    }
    if (upload_owner && upload_owner != wsi) {
      queue_error(wsi, session, request_id, 409, "Conflict",
                  "another firmware upload is active");
      return 0;
    }
    cleanup_upload(wsi, session);
    session->upload_handed_off = false;
    snprintf(session->upload_path, sizeof(session->upload_path),
             "/tmp/postmerkos-upload-%ld-%lx.bin", (long)getpid(),
             (unsigned long)wsi);
    session->upload_fd = open(session->upload_path,
                              O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (session->upload_fd < 0) {
      queue_error(wsi, session, request_id, 500, "Upload initialization failed",
                  strerror(errno));
      cleanup_upload(wsi, session);
      return 0;
    }
    snprintf(session->upload_name, sizeof(session->upload_name), "%s",
             name && *name ? name : "firmware.bin");
    snprintf(session->upload_sha256, sizeof(session->upload_sha256), "%s",
             sha256);
    snprintf(session->upload_overlay, sizeof(session->upload_overlay), "%s",
             overlay);
    session->upload_expected = (size_t)size_value;
    session->upload_received = 0;
    session->upload_force = force;
    session->upload_active = true;
    upload_owner = wsi;
    struct json_object *ready = upload_status_json(session);
    queue_response(wsi, session, "firmware_upload_ready", ready, request_id);
    json_object_put(ready);
    return 0;
  }

  if (!strcmp(type, "firmware_upload_finish")) {
    if (!session->upload_active || session->upload_fd < 0) {
      queue_bad_request(wsi, session, request_id, "no firmware upload is active");
      return 0;
    }
    if (fsync(session->upload_fd) != 0) {
      queue_error(wsi, session, request_id, 500, "Upload failed",
                  strerror(errno));
      cleanup_upload(wsi, session);
      return 0;
    }
    close(session->upload_fd);
    session->upload_fd = -1;
    if (session->upload_received != session->upload_expected) {
      queue_bad_request(wsi, session, request_id,
                        "uploaded byte count does not match the declared size");
      cleanup_upload(wsi, session);
      return 0;
    }
    char error[256] = {0};
    if (firmware_start_update(session->upload_path, session->upload_sha256,
                              session->upload_overlay, session->upload_force,
                              error, sizeof(error)) != 0) {
      queue_error(wsi, session, request_id, 500,
                  "Firmware update could not be started", error);
      cleanup_upload(wsi, session);
      return 0;
    }
    session->upload_active = false;
    session->upload_handed_off = true;
    upload_owner = NULL;
    struct json_object *started = json_object_new_object();
    json_object_object_add(started, "message", json_object_new_string(
        "Firmware update started. The management connection will close while flash is written and the switch reboots."));
    json_object_object_add(started, "name",
                           json_object_new_string(session->upload_name));
    queue_response(wsi, session, "firmware_started", started, request_id);
    json_object_put(started);
    return 0;
  }

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

  if (!strcmp(type, "replace_config")) {
    struct json_object *candidate = request_data_object(message);
    if (!candidate) {
      queue_bad_request(wsi, session, request_id,
                        "replace_config.data must be a complete configuration object");
      return 0;
    }
    struct apply_result result;
    apply_result_init(&result);
    char error[256] = "";
    int rc = replace_configuration(candidate, &result, error, sizeof(error));
    if (rc != 0) {
      queue_bad_request(wsi, session, request_id,
                        error[0] ? error : "configuration replacement failed");
      apply_result_cleanup(&result);
      return 0;
    }
    struct json_object *ack = apply_result_json(&result,
                                                "Configuration restored");
    queue_response(wsi, session, "ack", ack, request_id);
    json_object_put(ack);
    apply_result_cleanup(&result);
    config_file_mtime(&config_mtime_ns);
    refresh_config_cache(candidate, true);
    refresh_status_cache(true);
    lws_sul_schedule(ws_context, 0, &network_sul, network_poll_cb,
                     1 * LWS_USEC_PER_SEC);
    return 0;
  }

  if (strcmp(type, "config")) {
    queue_bad_request(wsi, session, request_id, "unknown request type");
    return 0;
  }

  struct json_object *delta = request_data_object(message);
  if (!delta) {
    queue_bad_request(wsi, session, request_id,
                      "config.data must be a JSON object");
    return 0;
  }

  struct apply_result result;
  apply_result_init(&result);
  struct json_object *saved = NULL;
  char error[256] = {0};
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

static int receive_binary(struct lws *wsi, struct per_session_data *session,
                          const void *input, size_t length) {
  if (!session->authenticated || !session->upload_active ||
      session->upload_fd < 0 || upload_owner != wsi) {
    queue_error(wsi, session, NULL, 400, "Unexpected binary data",
                "start an authenticated firmware upload first");
    return 0;
  }
  if (session->upload_received + length > session->upload_expected) {
    queue_bad_request(wsi, session, NULL,
                      "firmware upload exceeds the declared size");
    cleanup_upload(wsi, session);
    return 0;
  }
  const unsigned char *cursor = input;
  size_t remaining = length;
  while (remaining) {
    ssize_t written = write(session->upload_fd, cursor, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      queue_error(wsi, session, NULL, 500, "Firmware upload failed",
                  strerror(errno));
      cleanup_upload(wsi, session);
      return 0;
    }
    if (written == 0) {
      queue_error(wsi, session, NULL, 500, "Firmware upload failed",
                  "temporary upload file stopped accepting data");
      cleanup_upload(wsi, session);
      return 0;
    }
    cursor += written;
    remaining -= (size_t)written;
    session->upload_received += (size_t)written;
  }
  return 0;
}

static int receive_fragment(struct lws *wsi, struct per_session_data *session,
                            const void *input, size_t length) {
  if (lws_frame_is_binary(wsi))
    return receive_binary(wsi, session, input, length);

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
    case LWS_CALLBACK_ESTABLISHED: {
      if (client_count >= MAX_CLIENTS) {
        queue_bad_request(wsi, session, NULL, "server client limit reached");
        return -1;
      }
      memset(session, 0, sizeof(*session));
      session->upload_fd = -1;
      add_client(wsi);
      struct json_object *data = json_object_new_object();
      json_object_object_add(data, "message", json_object_new_string(
          "Linux PAM authentication is required"));
      queue_response(wsi, session, "auth_required", data, NULL);
      json_object_put(data);
      printf("ws: client connected (%zu total)\n", client_count);
      break;
    }

    case LWS_CALLBACK_CLOSED:
      remove_client(wsi);
      cleanup_upload(wsi, session);
      reset_receive(session);
      free_replies(session);
      printf("ws: client disconnected (%zu total)\n", client_count);
      break;

    case LWS_CALLBACK_RECEIVE:
      receive_fragment(wsi, session, input, length);
      break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
      if (session->reply_head) {
        struct queued_reply *reply = session->reply_head;
        session->reply_head = reply->next;
        if (!session->reply_head) session->reply_tail = NULL;
        ws_send(wsi, reply->text);
        free(reply->text);
        free(reply);
      } else if (session->authenticated && session->send_initial_status &&
                 cached_status_json) {
        ws_send(wsi, cached_status_json);
        session->send_initial_status = false;
      } else if (session->authenticated && session->send_initial_config &&
                 cached_config_json) {
        ws_send(wsi, cached_config_json);
        session->send_initial_config = false;
      } else if (session->authenticated && session->send_status &&
                 cached_status_json) {
        ws_send(wsi, cached_status_json);
        session->send_status = false;
      } else if (session->authenticated && session->send_config &&
                 cached_config_json) {
        ws_send(wsi, cached_config_json);
        session->send_config = false;
      }
      if (session->reply_head ||
          (session->authenticated &&
           (session->send_initial_status || session->send_initial_config ||
            session->send_status || session->send_config)))
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
    if (!session || !session->authenticated) continue;
    if (status_pending) session->send_status = true;
    if (config_pending) session->send_config = true;
    lws_callback_on_writable(clients[i]);
  }
  status_pending = false;
  config_pending = false;
}

static const struct lws_protocols protocols[] = {
  {
    .name = "configd-ws",
    .callback = configd_ws_callback,
    .per_session_data_size = sizeof(struct per_session_data),
    .rx_buffer_size = MAX_MSG_LEN,
  },
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
