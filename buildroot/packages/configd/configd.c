#include <libpostmerkos.h>
#include <libpd690xx.h>
#include "pd690xx_meraki.h"
#include "configd.h"

#include <dirent.h>
#include <json-c/json.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <libwebsockets.h>

#define MAX_CLIENTS 8
#define MAX_MSG_LEN 65536

bool poe_capable;
bool dry_run = false;
struct pd690xx_cfg pd690xx = {
    // i2c_fds
    {-1, -1},
    // pd690xx_addrs
    {PD690XX0_I2C_ADDR, PD690XX1_I2C_ADDR, PD690XX2_I2C_ADDR, PD690XX3_I2C_ADDR},
    // pd690xx_pres
    {0, 0, 0 ,0}
};

static char *config_file = "/etc/switch.json";
static struct lws_context *ws_context;

// connected client tracking
static struct lws *clients[MAX_CLIENTS];
static int client_count = 0;

// pending broadcast flags
static bool status_pending = false;
static bool config_pending = false;

// cached JSON strings for broadcast
static char *cached_status_json = NULL;
static char *cached_config_json = NULL;

// config file mtime tracking
static time_t config_mtime;

// scheduled timers
static struct lws_sorted_usec_list status_sul;
static struct lws_sorted_usec_list config_sul;

static void add_client(struct lws *wsi) {
  if (client_count < MAX_CLIENTS) {
    clients[client_count++] = wsi;
  }
}

static void remove_client(struct lws *wsi) {
  for (int i = 0; i < client_count; i++) {
    if (clients[i] == wsi) {
      clients[i] = clients[--client_count];
      return;
    }
  }
}

static void request_writable_all(void) {
  for (int i = 0; i < client_count; i++) {
    lws_callback_on_writable(clients[i]);
  }
}

// read device name from /etc/boardinfo
static const char *get_device_name(void) {
  static char name[64];
  FILE *f = fopen(DEVICE_FILE, "r");
  if (!f) return NULL;
  if (fgets(name, sizeof(name), f)) {
    name[strcspn(name, "\n")] = 0;
  }
  fclose(f);
  return name[0] ? name : NULL;
}

// build status JSON (absorbs clickswstatus logic)
struct json_object *get_status(void) {
  struct json_object *jobj = json_object_new_object();
  json_object_object_add(jobj, "datetime", json_object_new_string(get_time()));

  const char *device = get_device_name();
  if (device) {
    json_object_object_add(jobj, "device", json_object_new_string(device));
  }

  struct json_object *jtemp = json_object_new_object();
  json_object_object_add(jobj, "temperature", jtemp);

  struct json_object *jtempsys = json_object_new_array();
  json_object_object_add(jtemp, "cpu", jtempsys);

  struct dirent *dp;
  DIR *dfd;
  char *dir = "/sys/class/thermal";
  if ((dfd = opendir(dir)) != NULL) {
    char filename[100];
    while ((dp = readdir(dfd)) != NULL) {
      struct stat stbuf;
      sprintf(filename, "%s/%s", dir, dp->d_name);
      if (stat(filename, &stbuf) == -1)
        continue;
      if (!starts_with(filename + strlen(dir) + 1, "thermal_"))
        continue;
      strcat(filename, "/temp");
      FILE *file = fopen(filename, "r");
      if (!file) continue;
      static char line[100];
      fgets(line, sizeof(line), file);
      line[strcspn(line, "\n")] = 0;
      json_object_array_add(jtempsys, json_object_new_int(atoi(line) / 1000));
      fclose(file);
    }
    closedir(dfd);
  }

  if (poe_capable) {
    struct json_object *jtemppoe = json_object_new_array();
    json_object_object_add(jtemp, "poe", jtemppoe);
    float *temps = get_temp(&pd690xx);
    for (int i = 0; i < pd690xx_pres_count(&pd690xx); i++) {
      json_object_array_add(jtemppoe, json_object_new_double(temps[i]));
    }
    free(temps);
  }

  struct json_object *jports = json_object_new_object();
  json_object_object_add(jobj, "ports", jports);

  FILE *file = fopen(PORTS_FILE, "r");
  if (file) {
    char line[256];
    int p = -1;
    char buffer[256];
    while (fgets(line, sizeof(line), file)) {
      p++;
      if (p == 0) continue; // skip header

      struct json_object *jport = json_object_new_object();
      json_object_object_add(jports, itoa(p, buffer, 10), jport);

      struct json_object *jportlink = json_object_new_object();
      json_object_object_add(jport, "link", jportlink);

      json_object_object_add(jportlink, "established",
                             json_object_new_boolean(atoi(get_field(line, 2))));
      json_object_object_add(jportlink, "speed",
                             json_object_new_int(atoi(get_field(line, 3))));

      if (poe_capable) {
        struct json_object *jportpoe = json_object_new_object();
        json_object_object_add(jport, "poe", jportpoe);
        json_object_object_add(jportpoe, "power",
                               json_object_new_double(port_power(&pd690xx, p)));
      }
    }
    fclose(file);
  }

  return jobj;
}

// read config from click filesystem
struct json_object *read_config() {

  struct json_object *jobj = json_object_new_object();

  struct json_object *jports = json_object_new_object();
  json_object_object_add(jobj, "ports", jports);


  FILE *pports = fopen(PORTS_FILE, "r");
  char pports_line[256];

  FILE *phy_cfgs = fopen("/click/switch_port_table/dump_port_phy_cfgs", "r");
  char phy_cfgs_line[256];

  int p = -1;
  char buffer[256];
  while (fgets(pports_line, sizeof(pports_line), pports)) {
    fgets(phy_cfgs_line, sizeof(phy_cfgs_line), phy_cfgs);

    p++;
    if (p == 0) {
      // skip file header
      continue;
    }

    struct json_object *jport = json_object_new_object();
    json_object_object_add(jports, itoa(p, buffer, 10), jport);

    bool enabled;
    if (strcmp(get_field(phy_cfgs_line, 2), "aneg") == 0) {
        enabled = true;
    }
    json_object_object_add(jport, "enabled",
                           json_object_new_boolean(enabled));

    if (poe_capable) {
      struct json_object *jportpoe = json_object_new_object();
      json_object_object_add(jport, "poe", jportpoe);
      json_object_object_add(jportpoe, "enabled",
                             json_object_new_boolean(port_state(&pd690xx, p)));
      json_object_object_add(jportpoe, "mode",
                             json_object_new_string(port_type_str(&pd690xx, p)));
    }
  }

  fclose(pports);
  fclose(phy_cfgs);
  return jobj;
}

// write config to click filesystem
int write_config(struct json_object *json) {

  // get all top-level keys
  json_object_object_foreach(json, key, value) {

    // for the "ports" key
    if (strcmp(key, "ports") == 0) {
      // get all ports
      json_object_object_foreach(value, port, portconfig) {

        // get config for a port
        json_object_object_foreach(portconfig, item, itemconfig) {

          // for the "enabled" key
          if (strcmp(item, "enabled") == 0) {
            bool enabled = json_object_get_boolean(itemconfig);
            const char* state = get_field(read_switch_port_table("dump_port_phy_cfgs", atoi(port)), 2);

            if (enabled && (strcmp(state, "off") == 0)) {
              printf("port %s enabled\n", port);
              if (!dry_run) {
                char command[40];
                snprintf(command, 40, "PORT %s, MODE aneg", port);
                write_switch_port_table("set_port_phy_cfgs", command);
              }
            } else if (!enabled && (strcmp(state, "aneg") == 0)) {
              printf("port %s disabled\n", port);
              if (!dry_run) {
                char command[40];
                snprintf(command, 40, "PORT %s, MODE off", port);
                write_switch_port_table("set_port_phy_cfgs", command);
              }
            }
          }

          // for the "poe" key
          if (poe_capable && strcmp(item, "poe") == 0) {
            // get poe config
            json_object_object_foreach(itemconfig, poeitem, poeitemconfig) {

              // for the poe "enabled" key
              if (strcmp(poeitem, "enabled") == 0) {
                bool enabled = json_object_get_boolean(poeitemconfig);
                bool state = port_state(&pd690xx, atoi(port));

                if (enabled && (state == PORT_DISABLED)) {
                  printf("port %s poe enabled\n", port);
                  if (!dry_run) {
                    port_enable(&pd690xx, atoi(port));
                  }
                } else if (!enabled && (state == PORT_ENABLED)) {
                  printf("port %s poe disabled\n", port);
                  if (!dry_run) {
                    port_disable(&pd690xx, atoi(port));
                  }
                }
              }

            }
          }

        }

      }
    }

  }
  return 0;
}

// wrap a JSON object in a typed message envelope: {"type":"...","data":...}
static char *wrap_message(const char *type, struct json_object *data) {
  struct json_object *msg = json_object_new_object();
  json_object_object_add(msg, "type", json_object_new_string(type));
  json_object_object_add(msg, "data", json_object_get(data));
  const char *str = json_object_to_json_string_ext(msg, JSON_C_TO_STRING_PLAIN);
  char *result = strdup(str);
  json_object_put(msg);
  return result;
}

// send a JSON string over WebSocket with LWS_PRE padding
static int ws_send(struct lws *wsi, const char *json_str) {
  size_t len = strlen(json_str);
  unsigned char *buf = malloc(LWS_PRE + len);
  if (!buf) return -1;
  memcpy(buf + LWS_PRE, json_str, len);
  int written = lws_write(wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
  free(buf);
  return written;
}

// status polling callback (lws_sul)
static void status_poll_cb(struct lws_sorted_usec_list *sul) {
  struct json_object *status = get_status();
  char *msg = wrap_message("status", status);

  // compare with cached; only broadcast if changed
  if (!cached_status_json || strcmp(cached_status_json, msg) != 0) {
    free(cached_status_json);
    cached_status_json = msg;
    status_pending = true;
    request_writable_all();
  } else {
    free(msg);
  }

  json_object_put(status);

  // reschedule
  lws_sul_schedule(ws_context, 0, sul, status_poll_cb,
                   3 * LWS_USEC_PER_SEC);
}

// config file polling callback (lws_sul)
static void config_poll_cb(struct lws_sorted_usec_list *sul) {
  struct stat st;
  if (stat(config_file, &st) == 0 && difftime(st.st_mtime, config_mtime) > 0) {
    config_mtime = st.st_mtime;
    struct json_object *modified = json_object_from_file(config_file);
    if (modified) {
      write_config(modified);

      char *msg = wrap_message("config", modified);
      free(cached_config_json);
      cached_config_json = msg;
      config_pending = true;
      request_writable_all();

      json_object_put(modified);
    }
  }

  // reschedule
  lws_sul_schedule(ws_context, 0, sul, config_poll_cb,
                   10 * LWS_USEC_PER_SEC);
}

// per-session data for tracking what this client needs
struct per_session_data {
  bool send_initial_status;
  bool send_initial_config;
  bool send_status;
  bool send_config;
};

static int
configd_ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                    void *user, void *in, size_t len) {
  struct per_session_data *pss = (struct per_session_data *)user;

  switch (reason) {
  case LWS_CALLBACK_ESTABLISHED:
    add_client(wsi);
    pss->send_initial_status = true;
    pss->send_initial_config = true;
    pss->send_status = false;
    pss->send_config = false;
    lws_callback_on_writable(wsi);
    printf("ws: client connected (%d total)\n", client_count);
    break;

  case LWS_CALLBACK_CLOSED:
    remove_client(wsi);
    printf("ws: client disconnected (%d total)\n", client_count);
    break;

  case LWS_CALLBACK_RECEIVE: {
    // parse incoming message
    struct json_object *msg = json_tokener_parse((const char *)in);
    if (!msg) break;

    struct json_object *type_obj;
    if (!json_object_object_get_ex(msg, "type", &type_obj)) {
      json_object_put(msg);
      break;
    }

    const char *type = json_object_get_string(type_obj);
    if (strcmp(type, "config") == 0) {
      struct json_object *data_obj;
      if (json_object_object_get_ex(msg, "data", &data_obj)) {
        // write config to disk
        const char *json_str = json_object_to_json_string_ext(
            data_obj, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
        if (dry_run) {
          printf("[dry-run] would write config to %s:\n%s\n", config_file, json_str);
        } else {
          FILE *f = fopen(config_file, "w");
          if (f) {
            fprintf(f, "%s", json_str);
            fclose(f);
          }
        }

        // apply config to /click
        write_config(data_obj);

        // update mtime tracking
        struct stat st;
        if (stat(config_file, &st) == 0) {
          config_mtime = st.st_mtime;
        }

        // broadcast config to all clients
        char *broadcast = wrap_message("config", data_obj);
        free(cached_config_json);
        cached_config_json = broadcast;
        config_pending = true;
        request_writable_all();
      }
    }

    json_object_put(msg);
    break;
  }

  case LWS_CALLBACK_SERVER_WRITEABLE:
    // send initial status on connect
    if (pss->send_initial_status && cached_status_json) {
      ws_send(wsi, cached_status_json);
      pss->send_initial_status = false;
      if (pss->send_initial_config || pss->send_status || pss->send_config)
        lws_callback_on_writable(wsi);
      break;
    }
    // send initial config on connect
    if (pss->send_initial_config) {
      struct json_object *cfg = json_object_from_file(config_file);
      if (cfg) {
        char *msg = wrap_message("config", cfg);
        ws_send(wsi, msg);
        free(msg);
        json_object_put(cfg);
      }
      pss->send_initial_config = false;
      if (pss->send_status || pss->send_config)
        lws_callback_on_writable(wsi);
      break;
    }
    // broadcast status update
    if (pss->send_status && cached_status_json) {
      ws_send(wsi, cached_status_json);
      pss->send_status = false;
      if (pss->send_config)
        lws_callback_on_writable(wsi);
      break;
    }
    // broadcast config update
    if (pss->send_config && cached_config_json) {
      ws_send(wsi, cached_config_json);
      pss->send_config = false;
      break;
    }
    break;

  default:
    break;
  }

  return 0;
}

// mark all clients as having pending data when broadcasts are flagged
static void mark_clients_pending(void) {
  if (!status_pending && !config_pending)
    return;

  for (int i = 0; i < client_count; i++) {
    struct per_session_data *pss =
        (struct per_session_data *)lws_wsi_user(clients[i]);
    if (pss) {
      if (status_pending) pss->send_status = true;
      if (config_pending) pss->send_config = true;
    }
  }

  status_pending = false;
  config_pending = false;
}

static const struct lws_protocols protocols[] = {
    {
        "configd-ws",
        configd_ws_callback,
        sizeof(struct per_session_data),
        MAX_MSG_LEN,
    },
    LWS_PROTOCOL_LIST_TERM
};

int main(int argc, char **argv) {

  int ws_port = 4001;
  int status_interval = 3;
  int c;

  while ((c = getopt(argc, argv, "c:dp:w:")) != -1) {
    switch (c) {
    case 'c':
      config_file = optarg;
      break;
    case 'd':
      dry_run = true;
      break;
    case 'p':
      status_interval = atoi(optarg);
      break;
    case 'w':
      ws_port = atoi(optarg);
      break;
    }
  }

  // determine whether board is poe capable
  i2c_init(&pd690xx);
  if (pd690xx_pres_count(&pd690xx)) {
    poe_capable = true;
  }

  if (dry_run) {
    printf("configd: dry-run mode enabled, no changes will be made\n");
  }

  // create config file if it doesn't exist
  if (access(config_file, F_OK) != 0) {
    if (dry_run) {
      printf("[dry-run] would create config file at %s\n", config_file);
    } else {
      printf("new config file created at %s\n", config_file);
      FILE *file = fopen(config_file, "w");
      const char *json = json_object_to_json_string_ext(
          read_config(), JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
      fprintf(file, "%s", json);
      fclose(file);
    }
  }

  // initialize config mtime
  struct stat st;
  if (stat(config_file, &st) == 0) {
    config_mtime = st.st_mtime;
  }

  // seed initial status cache
  struct json_object *initial_status = get_status();
  cached_status_json = wrap_message("status", initial_status);
  json_object_put(initial_status);

  // set up lws context
  struct lws_context_creation_info info;
  memset(&info, 0, sizeof(info));
  info.port = ws_port;
  info.protocols = protocols;
  info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

  lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

  ws_context = lws_create_context(&info);
  if (!ws_context) {
    fprintf(stderr, "lws: context creation failed\n");
    return 1;
  }

  printf("configd: websocket server listening on port %d\n", ws_port);

  // schedule periodic timers
  lws_sul_schedule(ws_context, 0, &status_sul, status_poll_cb,
                   status_interval * LWS_USEC_PER_SEC);
  lws_sul_schedule(ws_context, 0, &config_sul, config_poll_cb,
                   10 * LWS_USEC_PER_SEC);

  // main event loop
  while (lws_service(ws_context, 0) >= 0) {
    mark_clients_pending();
  }

  lws_context_destroy(ws_context);
  free(cached_status_json);
  free(cached_config_json);

  return 0;
}
