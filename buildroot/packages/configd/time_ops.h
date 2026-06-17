#ifndef CONFIGD_TIME_OPS_H
#define CONFIGD_TIME_OPS_H
#include <json-c/json.h>
#include <stddef.h>
#include <time.h>

struct json_object *time_policy_load(void);
struct json_object *time_status_json(void);
int time_policy_save(struct json_object *policy, char *error, size_t error_size);
int time_policy_apply(char *error, size_t error_size);
int time_set_epoch(time_t epoch, char *error, size_t error_size);
int time_force_sync(char *error, size_t error_size);
#endif
