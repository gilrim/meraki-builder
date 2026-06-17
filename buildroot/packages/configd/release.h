#ifndef CONFIGD_RELEASE_H
#define CONFIGD_RELEASE_H
#include <json-c/json.h>
struct json_object *release_info_load(void);
const char *release_version(void);
#endif
