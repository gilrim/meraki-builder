#ifndef CONFIGD_CLICK_GLOBAL_H
#define CONFIGD_CLICK_GLOBAL_H

#include "result.h"
#include <json-c/json.h>

struct json_object *click_read_globals(struct apply_result *result);
int click_apply_globals_full(struct json_object *config,
                             struct apply_result *result);
int click_apply_globals_delta(struct json_object *full_config,
                              struct json_object *delta,
                              struct apply_result *result);

#endif
