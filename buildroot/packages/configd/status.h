#ifndef CONFIGD_STATUS_H
#define CONFIGD_STATUS_H

#include <json-c/json.h>

struct json_object *get_status(void);
struct json_object *reset_button_status_json(void);

#endif
