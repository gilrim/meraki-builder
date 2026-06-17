#include "roles.h"
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

static bool group_contains(const struct passwd *user, const char *name) {
  if (!user || !name) return false;
  struct group *group = getgrnam(name);
  if (!group) return false;
  if (user->pw_gid == group->gr_gid) return true;
  for (char **member = group->gr_mem; member && *member; member++)
    if (!strcmp(*member, user->pw_name)) return true;
  return false;
}

enum postmerkos_role role_for_username(const char *username) {
  if (!username || !*username) return POSTMERKOS_ROLE_NONE;
  struct passwd *user = getpwnam(username);
  if (!user) return POSTMERKOS_ROLE_NONE;
  if (user->pw_uid == 0 || group_contains(user, "postmerkos-admin"))
    return POSTMERKOS_ROLE_ADMIN;
  if (group_contains(user, "postmerkos-operator"))
    return POSTMERKOS_ROLE_OPERATOR;
  if (group_contains(user, "postmerkos-viewer"))
    return POSTMERKOS_ROLE_VIEWER;
  return POSTMERKOS_ROLE_NONE;
}

enum postmerkos_role role_for_uid(uid_t uid) {
  struct passwd *user = getpwuid(uid);
  return user ? role_for_username(user->pw_name) : POSTMERKOS_ROLE_NONE;
}

const char *role_name(enum postmerkos_role role) {
  switch (role) {
    case POSTMERKOS_ROLE_ADMIN: return "administrator";
    case POSTMERKOS_ROLE_OPERATOR: return "operator";
    case POSTMERKOS_ROLE_VIEWER: return "viewer";
    default: return "none";
  }
}

static const char *viewer_caps[] = {
  "status.read", "config.read", "firmware.history.read", NULL
};
static const char *operator_caps[] = {
  "status.read", "config.read", "firmware.history.read", "ports.write",
  "switching.write", "backup.create", "system.reboot", NULL
};
static const char *admin_caps[] = {
  "status.read", "config.read", "firmware.history.read", "ports.write",
  "switching.write", "backup.create", "system.reboot", "system.poweroff",
  "firmware.update", "config.restore", "network.write", "users.manage",
  "services.manage", "terminal.exec", "system.factory_reset", NULL
};

static const char *const *capabilities(enum postmerkos_role role) {
  if (role == POSTMERKOS_ROLE_ADMIN) return admin_caps;
  if (role == POSTMERKOS_ROLE_OPERATOR) return operator_caps;
  if (role == POSTMERKOS_ROLE_VIEWER) return viewer_caps;
  return NULL;
}

bool role_has_capability(enum postmerkos_role role, const char *capability) {
  if (!capability) return false;
  const char *const *list = capabilities(role);
  for (size_t i = 0; list && list[i]; i++)
    if (!strcmp(list[i], capability)) return true;
  return false;
}

struct json_object *role_capabilities_json(enum postmerkos_role role) {
  struct json_object *array = json_object_new_array();
  const char *const *list = capabilities(role);
  for (size_t i = 0; list && list[i]; i++)
    json_object_array_add(array, json_object_new_string(list[i]));
  return array;
}

struct json_object *role_identity_json(const char *username) {
  enum postmerkos_role role = role_for_username(username);
  struct json_object *identity = json_object_new_object();
  json_object_object_add(identity, "username",
      json_object_new_string(username ? username : ""));
  json_object_object_add(identity, "role", json_object_new_string(role_name(role)));
  json_object_object_add(identity, "capabilities", role_capabilities_json(role));
  return identity;
}
