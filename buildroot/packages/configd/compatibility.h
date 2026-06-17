#ifndef CONFIGD_COMPATIBILITY_H
#define CONFIGD_COMPATIBILITY_H
#include <json-c/json.h>
#include <stddef.h>
struct json_object *compatibility_report_json(void);
struct json_object *compatibility_notice_json(void);
int compatibility_acknowledge(char *error, size_t error_size);
#endif
