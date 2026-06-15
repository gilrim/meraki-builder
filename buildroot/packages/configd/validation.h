#ifndef CONFIGD_VALIDATION_H
#define CONFIGD_VALIDATION_H

#include <json-c/json.h>
#include <stddef.h>

int validate_configuration(struct json_object *config,
                           char *error, size_t error_size);

#endif
