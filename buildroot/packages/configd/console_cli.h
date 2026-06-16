#ifndef CONFIGD_CONSOLE_CLI_H
#define CONFIGD_CONSOLE_CLI_H

#include <json-c/json.h>
#include <stddef.h>

struct json_object *console_path_get(struct json_object *root,
                                     const char *path);
struct json_object *console_delta_from_path(const char *path,
                                            const char *value,
                                            char *error,
                                            size_t error_size);
struct json_object *console_delta_from_string_path(const char *path,
                                                   const char *value,
                                                   char *error,
                                                   size_t error_size);
int console_print_path(struct json_object *config, const char *path);
int console_print_summary(struct json_object *config);
int console_print_ports(struct json_object *config,
                        unsigned int first, unsigned int last);
int console_print_port(struct json_object *config, unsigned int port);
int console_export_config(struct json_object *config, const char *path,
                          char *error, size_t error_size);

#endif
