#define _GNU_SOURCE
#include "hardware.h"
#include <assert.h>
#include <json-c/json.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(void) {
  struct hardware_info info; struct pd690xx_cfg poe;
  memset(&poe, 0, sizeof(poe));
  setenv("CONFIGD_MODEL", "MS42P", 1); setenv("CONFIGD_SKIP_I2C", "1", 1);
  assert(hardware_init(&info, &poe) == 0);
  assert(info.port_count == 52 && info.copper_port_count == 48 && info.uplink_port_count == 4);
  assert(info.uplink_max_speed_mbps == 10000);
  assert(strcmp(info.uplink_label, "SFP+") == 0);
  struct json_object *caps = hardware_capabilities_json(&info); assert(caps);
  struct json_object *uplink = NULL, *label = NULL, *speed = NULL;
  assert(json_object_object_get_ex(caps, "uplink", &uplink));
  assert(json_object_object_get_ex(uplink, "label", &label));
  assert(json_object_object_get_ex(uplink, "max_speed_mbps", &speed));
  assert(strcmp(json_object_get_string(label), "SFP+") == 0);
  assert(json_object_get_int(speed) == 10000);
  json_object_put(caps);
  puts("hardware metadata tests passed"); return 0;
}
