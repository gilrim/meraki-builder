#ifndef CONFIGD_HARDWARE_H
#define CONFIGD_HARDWARE_H

#include <json-c/json.h>
#include <stdbool.h>
#include <libpd690xx.h>

struct hardware_info {
  char model[32];
  unsigned int port_count;
  unsigned int poe_port_count;
  unsigned int poe_controller_count;
  bool poe_supported;
  bool poe_available;
};

int hardware_init(struct hardware_info *info, struct pd690xx_cfg *pd690xx);
bool hardware_port_valid(const struct hardware_info *info, unsigned int port);
bool hardware_port_supports_poe(const struct hardware_info *info,
                                unsigned int port);
struct json_object *hardware_capabilities_json(const struct hardware_info *info);

#endif
