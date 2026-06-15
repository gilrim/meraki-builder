#ifndef CONFIGD_RESULT_H
#define CONFIGD_RESULT_H

#include <json-c/json.h>
#include <stdarg.h>

struct apply_result {
  struct json_object *warnings;
  unsigned int applied;
};

void apply_result_init(struct apply_result *result);
void apply_result_cleanup(struct apply_result *result);
void apply_result_warn(struct apply_result *result, const char *format, ...);
void apply_result_applied(struct apply_result *result);
struct json_object *apply_result_json(const struct apply_result *result,
                                      const char *message);

#endif
