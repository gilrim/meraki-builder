#ifndef CONFIGD_PORT_CLONE_H
#define CONFIGD_PORT_CLONE_H
#include <json-c/json.h>
#include <stddef.h>
int port_clone_apply(struct json_object *request, struct json_object **result,
                     char *error, size_t error_size);
#endif
