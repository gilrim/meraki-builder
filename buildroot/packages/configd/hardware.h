#ifndef CONFIGD_HARDWARE_H
#define CONFIGD_HARDWARE_H

#include <json-c/json.h>
#include <stdbool.h>
#include <libpd690xx.h>

enum compatibility_state {
  COMPATIBILITY_CONFIRMED = 0,
  COMPATIBILITY_UNTESTED,
  COMPATIBILITY_INCOMPATIBLE
};

struct hardware_info {
  char model[32];
  char family[32];
  unsigned int port_count;
  unsigned int copper_port_count;
  unsigned int uplink_port_count;
  unsigned int poe_port_count;
  unsigned int poe_controller_count;
  unsigned int switch_instances;
  unsigned int uplink_max_speed_mbps;
  char uplink_media[16];
  char uplink_label[16];
  bool poe_supported;
  bool poe_available;
  enum compatibility_state compatibility;
};

int hardware_init(struct hardware_info *info, struct pd690xx_cfg *pd690xx);
bool hardware_port_valid(const struct hardware_info *info, unsigned int port);
bool hardware_port_supports_poe(const struct hardware_info *info,
                                unsigned int port);
const char *hardware_compatibility_name(enum compatibility_state state);
struct json_object *hardware_capabilities_json(const struct hardware_info *info);

#endif
