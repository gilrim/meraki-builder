#ifndef CONFIGD_CLICK_PORT_H
#define CONFIGD_CLICK_PORT_H

#include "result.h"
#include <json-c/json.h>
#include <stdbool.h>

struct json_object *click_read_ports(struct apply_result *result);
int click_apply_ports_full(struct json_object *config,
                           struct apply_result *result);
int click_apply_ports_delta(struct json_object *full_config,
                            struct json_object *delta,
                            struct apply_result *result);

#endif
