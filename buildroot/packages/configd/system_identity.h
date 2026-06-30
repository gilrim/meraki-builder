#ifndef CONFIGD_SYSTEM_IDENTITY_H
#define CONFIGD_SYSTEM_IDENTITY_H

#include <json-c/json.h>
#include <stddef.h>

struct json_object *system_identity_policy_load(void);
struct json_object *system_identity_status_json(void);
int system_identity_save(struct json_object *policy, char *error, size_t error_size);
int system_identity_apply(char *error, size_t error_size);
int system_identity_validate_hostname(const char *hostname, char *error, size_t error_size);

#endif
