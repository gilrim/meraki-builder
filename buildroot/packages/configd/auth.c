#include "auth.h"

#include <crypt.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void set_error(char *error, size_t size, const char *message) {
  if (error && size) snprintf(error, size, "%s", message ? message : "error");
}

static bool safe_username(const char *username) {
  if (!username || !*username || strlen(username) > 64) return false;
  for (const unsigned char *p = (const unsigned char *)username; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.'))
      return false;
  }
  return true;
}

static bool user_is_admin(const struct passwd *entry) {
  if (!entry) return false;
  if (entry->pw_uid == 0) return true;
  struct group *admin = getgrnam("postmerkos-admin");
  if (!admin) return false;
  if (entry->pw_gid == admin->gr_gid) return true;
  for (char **member = admin->gr_mem; member && *member; member++)
    if (!strcmp(*member, entry->pw_name)) return true;
  return false;
}

static bool constant_time_equal(const char *left, const char *right) {
  if (!left || !right) return false;
  size_t left_length = strlen(left);
  size_t right_length = strlen(right);
  size_t length = left_length > right_length ? left_length : right_length;
  unsigned int difference = (unsigned int)(left_length ^ right_length);
  for (size_t i = 0; i < length; i++) {
    unsigned char a = i < left_length ? (unsigned char)left[i] : 0;
    unsigned char b = i < right_length ? (unsigned char)right[i] : 0;
    difference |= (unsigned int)(a ^ b);
  }
  return difference == 0;
}

static const char *password_hash(const struct passwd *entry) {
  if (!entry) return NULL;
  if (entry->pw_passwd && entry->pw_passwd[0] &&
      strcmp(entry->pw_passwd, "x") && strcmp(entry->pw_passwd, "*"))
    return entry->pw_passwd;
  struct spwd *shadow = getspnam(entry->pw_name);
  return shadow ? shadow->sp_pwdp : NULL;
}

int auth_verify_user(const char *username, const char *password,
                     char *error, size_t error_size) {
  if (!safe_username(username) || !password) {
    set_error(error, error_size, "invalid credentials");
    return -EINVAL;
  }
  struct passwd *entry = getpwnam(username);
  if (!entry || !user_is_admin(entry)) {
    set_error(error, error_size,
              "invalid credentials or account is not authorized for switch management");
    return -EACCES;
  }
  const char *hash = password_hash(entry);
  if (!hash || !*hash || hash[0] == '!' || hash[0] == '*') {
    set_error(error, error_size, "account password is locked or unavailable");
    return -EACCES;
  }
  char *calculated = crypt(password, hash);
  if (!calculated || !constant_time_equal(calculated, hash)) {
    set_error(error, error_size, "authentication failed");
    return -EACCES;
  }
  return 0;
}

struct json_object *auth_list_users(void) {
  struct json_object *users = json_object_new_array();
  setpwent();
  struct passwd *entry;
  while ((entry = getpwent()) != NULL) {
    if (!entry->pw_name || !safe_username(entry->pw_name) ||
        !user_is_admin(entry)) continue;
    const char *shell = entry->pw_shell ? entry->pw_shell : "";
    if (strstr(shell, "nologin") || strstr(shell, "false")) continue;
    struct json_object *user = json_object_new_object();
    json_object_object_add(user, "username",
                           json_object_new_string(entry->pw_name));
    json_object_object_add(user, "uid",
                           json_object_new_int64((int64_t)entry->pw_uid));
    json_object_object_add(user, "home",
                           json_object_new_string(entry->pw_dir ? entry->pw_dir : ""));
    json_object_object_add(user, "shell", json_object_new_string(shell));
    json_object_array_add(users, user);
  }
  endpwent();
  return users;
}

static int run_chpasswd(const char *username, const char *new_password,
                        char *error, size_t error_size) {
  int input_pipe[2];
  if (pipe(input_pipe) != 0) {
    set_error(error, error_size, strerror(errno));
    return -errno;
  }
  pid_t child = fork();
  if (child < 0) {
    int saved = errno;
    close(input_pipe[0]);
    close(input_pipe[1]);
    set_error(error, error_size, strerror(saved));
    return -saved;
  }
  if (child == 0) {
    dup2(input_pipe[0], STDIN_FILENO);
    int nullfd = open("/dev/null", O_WRONLY);
    if (nullfd >= 0) {
      dup2(nullfd, STDOUT_FILENO);
      dup2(nullfd, STDERR_FILENO);
      if (nullfd > STDERR_FILENO) close(nullfd);
    }
    close(input_pipe[0]);
    close(input_pipe[1]);
    setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
    execlp("chpasswd", "chpasswd", (char *)NULL);
    _exit(127);
  }
  close(input_pipe[0]);
  dprintf(input_pipe[1], "%s:%s\n", username, new_password);
  close(input_pipe[1]);
  int status = 0;
  if (waitpid(child, &status, 0) < 0) {
    set_error(error, error_size, strerror(errno));
    return -errno;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    set_error(error, error_size, "password update failed");
    return -EIO;
  }
  return 0;
}

int auth_change_password(const char *actor, const char *target,
                         const char *actor_password,
                         const char *new_password,
                         char *error, size_t error_size) {
  if (!safe_username(actor) || !safe_username(target)) {
    set_error(error, error_size, "unknown user");
    return -ENOENT;
  }
  struct passwd *target_entry = getpwnam(target);
  if (!target_entry || !user_is_admin(target_entry)) {
    set_error(error, error_size, "unknown or unauthorized target account");
    return -ENOENT;
  }
  if (!new_password || strlen(new_password) < 8 || strlen(new_password) > 128 ||
      strchr(new_password, '\n') || strchr(new_password, '\r') ||
      strchr(new_password, ':')) {
    set_error(error, error_size,
              "new password must be 8-128 characters and may not contain ':' or line breaks");
    return -EINVAL;
  }
  int rc = auth_verify_user(actor, actor_password, error, error_size);
  if (rc != 0) return rc;
  struct passwd *actor_entry = getpwnam(actor);
  if (!actor_entry) return -ENOENT;
  if (actor_entry->pw_uid != 0 && strcmp(actor, target)) {
    set_error(error, error_size, "only root may change another account");
    return -EACCES;
  }
  return run_chpasswd(target, new_password, error, error_size);
}
