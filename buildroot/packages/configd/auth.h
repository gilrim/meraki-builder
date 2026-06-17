#ifndef CONFIGD_AUTH_H
#define CONFIGD_AUTH_H

#include <json-c/json.h>
#include <stddef.h>

int auth_verify_user(const char *username, const char *password,
                     char *error, size_t error_size);
struct json_object *auth_list_users(void);
int auth_change_password(const char *actor, const char *target,
                         const char *actor_password,
                         const char *new_password,
                         char *error, size_t error_size);
int auth_create_user(const char *username, const char *password,
                     const char *role, char *error, size_t error_size);
int auth_delete_user(const char *username, char *error, size_t error_size);
int auth_set_role(const char *username, const char *role,
                  char *error, size_t error_size);

#endif
