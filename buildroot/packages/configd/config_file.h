#ifndef CONFIGD_CONFIG_FILE_H
#define CONFIGD_CONFIG_FILE_H

#include <json-c/json.h>
#include <stddef.h>

struct json_object *load_config_file(void);
struct json_object *load_json_file(const char *path);
int save_config_file(struct json_object *json, char *error, size_t error_size);
int config_file_mtime(long long *mtime_ns);
void config_file_set_runtime_error(const char *message);
const char *config_file_runtime_error(void);

#endif
