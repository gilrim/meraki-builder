#ifndef CONFIGD_JSON_UTIL_H
#define CONFIGD_JSON_UTIL_H

#include <json-c/json.h>
#include <stdbool.h>
#include <stddef.h>

void json_deep_merge(struct json_object *base, struct json_object *patch);
struct json_object *json_deep_copy_object(struct json_object *object);
bool json_object_has_any_key(struct json_object *object,
                             const char *const *keys, size_t key_count);

#endif
