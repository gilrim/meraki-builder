#include "click_global.h"
#include "click_port.h"
#include "config_apply.h"
#include "config_file.h"
#include "configd.h"
#include "network.h"
#include "result.h"
#include "status.h"
#include "validation.h"
#include "websocket.h"

#include <libpostmerkos.h>
#include <libpd690xx.h>
#include "pd690xx_meraki.h"

#include <errno.h>
#include <getopt.h>
#include <json-c/json.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool dry_run = false;
const char *config_file = "/etc/switch.json";
char meraki_mac[18] = "";
struct hardware_info hardware;
struct pd690xx_cfg pd690xx = {
  {-1, -1},
  {PD690XX0_I2C_ADDR, PD690XX1_I2C_ADDR,
   PD690XX2_I2C_ADDR, PD690XX3_I2C_ADDR},
  {0, 0, 0, 0}
};

static volatile sig_atomic_t running = 1;

static void signal_handler(int signal_number) {
  (void)signal_number;
  running = 0;
}

static void usage(FILE *stream, const char *program) {
  fprintf(stream,
      "Usage: %s [options]\n"
      "  -c, --config PATH          persistent configuration file\n"
      "  -d, --dry-run              log writes without changing hardware\n"
      "  -p, --status-interval SEC  status broadcast interval (1-3600)\n"
      "  -w, --websocket-port PORT  WebSocket listen port (1-65535)\n"
      "  -N, --network-bootstrap    apply DHCP/static management IP and exit\n"
      "  -W, --network-wait SEC     DHCP wait during bootstrap (0-300, default 60)\n"
      "      --get-config           print the current persistent JSON config\n"
      "      --get-status           print current status JSON\n"
      "      --validate FILE        validate a complete configuration file\n"
      "      --apply-file FILE      merge and apply a JSON configuration delta\n"
      "      --apply-json JSON      merge and apply an inline JSON delta\n"
      "  -h, --help                 show this help\n",
      program);
}

static int parse_int(const char *text, int minimum, int maximum, int *output) {
  char *end = NULL;
  errno = 0;
  long value = strtol(text, &end, 10);
  if (errno || !text || !*text || !end || *end ||
      value < minimum || value > maximum) return -EINVAL;
  *output = (int)value;
  return 0;
}

static void read_meraki_mac(void) {
  const char *paths[] = {"/tmp/MERAKI_MAC", "/etc/MERAKI_MAC"};
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    FILE *file = fopen(paths[i], "r");
    if (!file) continue;
    if (fgets(meraki_mac, sizeof(meraki_mac), file)) {
      meraki_mac[strcspn(meraki_mac, "\r\n")] = '\0';
      fclose(file);
      return;
    }
    fclose(file);
  }
}

static void print_envelope(const char *type, struct json_object *data) {
  struct json_object *message = json_object_new_object();
  json_object_object_add(message, "type", json_object_new_string(type));
  json_object_object_add(message, "data", json_object_get(data));
  puts(json_object_to_json_string_ext(message, JSON_C_TO_STRING_PRETTY));
  json_object_put(message);
}

static int print_bad_request(const char *detail) {
  struct json_object *data = json_object_new_object();
  json_object_object_add(data, "status", json_object_new_int(400));
  json_object_object_add(data, "message", json_object_new_string("Bad Request"));
  if (detail) json_object_object_add(data, "detail", json_object_new_string(detail));
  print_envelope("error", data);
  json_object_put(data);
  return 2;
}

enum command_mode {
  COMMAND_SERVICE,
  COMMAND_NETWORK_BOOTSTRAP,
  COMMAND_GET_CONFIG,
  COMMAND_GET_STATUS,
  COMMAND_VALIDATE,
  COMMAND_APPLY_FILE,
  COMMAND_APPLY_JSON,
};

int main(int argc, char **argv) {
  int websocket_port = 4001;
  int status_interval = 3;
  int network_wait = 60;
  enum command_mode command = COMMAND_SERVICE;
  const char *command_value = NULL;

  enum { OPT_GET_CONFIG = 1000, OPT_GET_STATUS, OPT_VALIDATE,
         OPT_APPLY_FILE, OPT_APPLY_JSON };
  static const struct option options[] = {
    {"config", required_argument, NULL, 'c'},
    {"dry-run", no_argument, NULL, 'd'},
    {"status-interval", required_argument, NULL, 'p'},
    {"websocket-port", required_argument, NULL, 'w'},
    {"network-bootstrap", no_argument, NULL, 'N'},
    {"network-wait", required_argument, NULL, 'W'},
    {"get-config", no_argument, NULL, OPT_GET_CONFIG},
    {"get-status", no_argument, NULL, OPT_GET_STATUS},
    {"validate", required_argument, NULL, OPT_VALIDATE},
    {"apply-file", required_argument, NULL, OPT_APPLY_FILE},
    {"apply-json", required_argument, NULL, OPT_APPLY_JSON},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
  };

  int option;
  while ((option = getopt_long(argc, argv, "c:dp:w:NW:h", options, NULL)) != -1) {
    switch (option) {
      case 'c': config_file = optarg; break;
      case 'd': dry_run = true; break;
      case 'p':
        if (parse_int(optarg, 1, 3600, &status_interval) != 0) {
          fprintf(stderr, "invalid status interval\n");
          return 2;
        }
        break;
      case 'w':
        if (parse_int(optarg, 1, 65535, &websocket_port) != 0) {
          fprintf(stderr, "invalid WebSocket port\n");
          return 2;
        }
        break;
      case 'N': command = COMMAND_NETWORK_BOOTSTRAP; break;
      case 'W':
        if (parse_int(optarg, 0, 300, &network_wait) != 0) {
          fprintf(stderr, "invalid network wait interval\n");
          return 2;
        }
        break;
      case OPT_GET_CONFIG: command = COMMAND_GET_CONFIG; break;
      case OPT_GET_STATUS: command = COMMAND_GET_STATUS; break;
      case OPT_VALIDATE: command = COMMAND_VALIDATE; command_value = optarg; break;
      case OPT_APPLY_FILE: command = COMMAND_APPLY_FILE; command_value = optarg; break;
      case OPT_APPLY_JSON: command = COMMAND_APPLY_JSON; command_value = optarg; break;
      case 'h': usage(stdout, argv[0]); return 0;
      default: usage(stderr, argv[0]); return 2;
    }
  }
  if (optind != argc) {
    usage(stderr, argv[0]);
    return 2;
  }

  /* S10 invokes bootstrap before the PoE init script. Keep this path
   * network-only: do not probe I2C and do not create the complete switch
   * configuration from a graph that is still receiving its baseline state. */
  if (command == COMMAND_NETWORK_BOOTSTRAP) {
    char network_error[256];
    struct json_object *config = load_config_file();
    if (!config || network_validate_config(config, network_error,
                                            sizeof(network_error)) != 0) {
      if (config) json_object_put(config);
      config = json_object_new_object();
      json_object_object_add(config, "network", network_default_config());
    }
    network_manager_observe(config);
    const struct network_runtime *observed = network_manager_runtime();
    if (!strcmp(observed->configured_mode, "dhcp") &&
        strcmp(observed->source, "dhcp") && network_wait > 0) {
      fprintf(stderr, "Waiting up to %d seconds for a DHCP lease", network_wait);
      fflush(stderr);
      for (int elapsed = 0; elapsed < network_wait; elapsed++) {
        sleep(1);
        network_manager_observe(config);
        observed = network_manager_runtime();
        if (!strcmp(observed->source, "dhcp")) break;
        fputc('.', stderr);
        fflush(stderr);
      }
      fputc('\n', stderr);
      observed = network_manager_runtime();
      if (!strcmp(observed->source, "dhcp")) {
        fprintf(stderr, "DHCP lease detected: %s/%u via %s\n",
                observed->applied.address, observed->applied.prefix,
                observed->applied.gateway);
      } else {
        fprintf(stderr,
                "No DHCP lease detected during bootstrap; applying fallback\n");
      }
    }

    struct apply_result result;
    apply_result_init(&result);
    int network_rc = network_manager_init(config, &result);
    const struct network_runtime *runtime = network_manager_runtime();
    if (runtime->applied.address[0]) {
      fprintf(stderr,
              "Management IPv4 configured: source=%s address=%s/%u gateway=%s broadcast=%s mtu=%u\n",
              runtime->source, runtime->applied.address,
              runtime->applied.prefix, runtime->applied.gateway,
              runtime->applied.broadcast, runtime->applied.mtu);
    }
    struct json_object *data = apply_result_json(&result,
                                                 "Network bootstrap complete");
    print_envelope("ack", data);
    json_object_put(data);
    apply_result_cleanup(&result);
    json_object_put(config);
    return network_rc == 0 ? 0 : 1;
  }

  hardware_init(&hardware, &pd690xx);
  read_meraki_mac();

  if (command == COMMAND_VALIDATE) {
    struct json_object *candidate = load_json_file(command_value);
    if (!candidate) return print_bad_request("unable to parse validation file");
    char error[256];
    int rc = validate_configuration(candidate, error, sizeof(error));
    json_object_put(candidate);
    if (rc != 0) return print_bad_request(error);
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "message",
                           json_object_new_string("Configuration is valid"));
    print_envelope("ack", data);
    json_object_put(data);
    i2c_close(&pd690xx);
    return 0;
  }

  char error[256];
  struct json_object *config = config_load_or_create(error, sizeof(error));
  if (!config) {
    fprintf(stderr, "configd: %s\n", error);
    i2c_close(&pd690xx);
    return 1;
  }

  if (command == COMMAND_GET_CONFIG) {
    print_envelope("config", config);
    json_object_put(config);
    i2c_close(&pd690xx);
    return 0;
  }

  struct apply_result startup;
  apply_result_init(&startup);
  if (command == COMMAND_GET_STATUS) {
    /* Populate network status without writing Click handlers from a read-only
     * CLI request. */
    network_manager_observe(config);
    struct json_object *status = get_status();
    print_envelope("status", status);
    json_object_put(status);
    apply_result_cleanup(&startup);
    json_object_put(config);
    i2c_close(&pd690xx);
    return 0;
  }

  network_manager_init(config, &startup);

  if (command == COMMAND_APPLY_FILE || command == COMMAND_APPLY_JSON) {
    struct json_object *delta = command == COMMAND_APPLY_FILE
        ? load_json_file(command_value) : json_tokener_parse(command_value);
    if (!delta) {
      apply_result_cleanup(&startup);
      json_object_put(config);
      i2c_close(&pd690xx);
      return print_bad_request("unable to parse configuration delta");
    }
    struct apply_result result;
    apply_result_init(&result);
    struct json_object *saved = NULL;
    int rc = config_merge_validate_save_apply(delta, &saved, &result, false,
                                               error, sizeof(error));
    json_object_put(delta);
    if (rc != 0) {
      apply_result_cleanup(&result);
      apply_result_cleanup(&startup);
      json_object_put(config);
      i2c_close(&pd690xx);
      return print_bad_request(error);
    }
    struct json_object *data = apply_result_json(&result,
                                                 "Configuration accepted");
    print_envelope("ack", data);
    json_object_put(data);
    json_object_put(saved);
    apply_result_cleanup(&result);
    apply_result_cleanup(&startup);
    json_object_put(config);
    i2c_close(&pd690xx);
    return 0;
  }

  /* Desired state is replayed on every daemon start. This is required for
   * write-only Click settings such as storm control. */
  click_apply_globals_full(config, &startup);
  click_apply_ports_full(config, &startup);
  if (json_object_array_length(startup.warnings) > 0)
    fprintf(stderr, "%s configd: startup completed with warnings: %s\n",
            get_time(), json_object_to_json_string(startup.warnings));
  apply_result_cleanup(&startup);
  json_object_put(config);

  struct lws_context *context = ws_init(websocket_port);
  if (!context) {
    fprintf(stderr, "configd: WebSocket context creation failed\n");
    i2c_close(&pd690xx);
    return 1;
  }
  ws_schedule_timers(context, status_interval);
  printf("configd: WebSocket server listening on port %d\n", websocket_port);

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  while (running && lws_service(context, 100) >= 0) mark_clients_pending();

  printf("configd: shutting down\n");
  ws_shutdown(context);
  i2c_close(&pd690xx);
  return 0;
}
