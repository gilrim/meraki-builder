#ifndef CONFIGD_SYSTEM_OPS_H
#define CONFIGD_SYSTEM_OPS_H

#include <json-c/json.h>
#include <stdbool.h>
#include <stddef.h>

struct json_object *terminal_execute(const char *command);
struct json_object *firmware_status_json(void);
int firmware_start_update(const char *path, const char *overlay, bool force,
                          char *error, size_t error_size);

#endif
