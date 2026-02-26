#ifndef CONFIG_H
#define CONFIG_H

#include <json-c/json.h>

struct json_object *read_config(void);
int write_config(struct json_object *json);

#endif
