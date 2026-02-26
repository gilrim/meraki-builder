#include "config.h"
#include "configd.h"

#include <libpostmerkos.h>
#include <libpd690xx.h>
#include "pd690xx_meraki.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// read config from click filesystem
struct json_object *read_config(void) {

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

// print a structured change log entry to stdout
static void log_change(int port, const char *field, const char *old_val, const char *new_val) {
  printf("%s port=%d field=%s old=%s new=%s%s\n",
         get_time(), port, field, old_val, new_val,
         dry_run ? " dry_run=true" : "");
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
              log_change(atoi(port), "enabled", "false", "true");
              if (!dry_run) {
                char command[40];
                snprintf(command, 40, "PORT %s, MODE aneg", port);
                write_switch_port_table("set_port_phy_cfgs", command);
              }
            } else if (!enabled && (strcmp(state, "aneg") == 0)) {
              log_change(atoi(port), "enabled", "true", "false");
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
                  log_change(atoi(port), "poe.enabled", "false", "true");
                  if (!dry_run) {
                    port_enable(&pd690xx, atoi(port));
                  }
                } else if (!enabled && (state == PORT_ENABLED)) {
                  log_change(atoi(port), "poe.enabled", "true", "false");
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
