#include "result.h"

#include <stdio.h>
#include <string.h>

void apply_result_init(struct apply_result *result) {
  if (!result) return;
  result->warnings = json_object_new_array();
  result->applied = 0;
}

void apply_result_cleanup(struct apply_result *result) {
  if (!result) return;
  if (result->warnings) json_object_put(result->warnings);
  result->warnings = NULL;
  result->applied = 0;
}

void apply_result_warn(struct apply_result *result, const char *format, ...) {
  if (!result || !result->warnings || !format) return;
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  json_object_array_add(result->warnings, json_object_new_string(buffer));
}

void apply_result_applied(struct apply_result *result) {
  if (result) result->applied++;
}

struct json_object *apply_result_json(const struct apply_result *result,
                                      const char *message) {
  struct json_object *data = json_object_new_object();
  json_object_object_add(data, "message",
      json_object_new_string(message ? message : "Configuration accepted"));
  json_object_object_add(data, "applied",
      json_object_new_int(result ? (int)result->applied : 0));
  if (result && result->warnings) {
    json_object_object_add(data, "warnings", json_object_get(result->warnings));
  } else {
    json_object_object_add(data, "warnings", json_object_new_array());
  }
  return data;
}
