#include "json_util.h"

#include <stdbool.h>
#include <stddef.h>

void json_deep_merge(struct json_object *base, struct json_object *patch) {
  if (!base || !patch || !json_object_is_type(base, json_type_object) ||
      !json_object_is_type(patch, json_type_object)) return;
  json_object_object_foreach(patch, key, patch_value) {
    if (json_object_is_type(patch_value, json_type_null)) {
      json_object_object_del(base, key);
      continue;
    }
    struct json_object *base_value = NULL;
    if (json_object_object_get_ex(base, key, &base_value) &&
        json_object_is_type(base_value, json_type_object) &&
        json_object_is_type(patch_value, json_type_object)) {
      json_deep_merge(base_value, patch_value);
    } else {
      json_object_object_add(base, key, json_object_get(patch_value));
    }
  }
}

struct json_object *json_deep_copy_object(struct json_object *object) {
  if (!object) return NULL;
  const char *text = json_object_to_json_string_ext(object,
                                                    JSON_C_TO_STRING_PLAIN);
  return json_tokener_parse(text);
}

bool json_object_has_any_key(struct json_object *object,
                             const char *const *keys, size_t key_count) {
  if (!object || !json_object_is_type(object, json_type_object)) return false;
  for (size_t i = 0; i < key_count; i++) {
    struct json_object *unused;
    if (json_object_object_get_ex(object, keys[i], &unused)) return true;
  }
  return false;
}
