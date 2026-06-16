#include "auth.h"

#include <security/pam_appl.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct pam_secret {
  const char *username;
  const char *password;
};

static void set_error(char *error, size_t size, const char *message) {
  if (error && size) snprintf(error, size, "%s", message ? message : "error");
}

static int conversation(int count, const struct pam_message **messages,
                        struct pam_response **responses, void *userdata) {
  if (count <= 0 || !messages || !responses || !userdata) return PAM_CONV_ERR;
  const struct pam_secret *secret = userdata;
  struct pam_response *reply = calloc((size_t)count, sizeof(*reply));
  if (!reply) return PAM_BUF_ERR;

  for (int i = 0; i < count; i++) {
    const char *value = NULL;
    switch (messages[i]->msg_style) {
      case PAM_PROMPT_ECHO_OFF:
        value = secret->password;
        break;
      case PAM_PROMPT_ECHO_ON:
        value = secret->username;
        break;
      case PAM_ERROR_MSG:
      case PAM_TEXT_INFO:
        value = NULL;
        break;
      default:
        for (int j = 0; j < i; j++) free(reply[j].resp);
        free(reply);
        return PAM_CONV_ERR;
    }
    if (value) {
      reply[i].resp = strdup(value);
      if (!reply[i].resp) {
        for (int j = 0; j <= i; j++) free(reply[j].resp);
        free(reply);
        return PAM_BUF_ERR;
      }
    }
  }
  *responses = reply;
  return PAM_SUCCESS;
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

static bool safe_username(const char *username) {
  if (!username || !*username || strlen(username) > 64) return false;
  for (const unsigned char *p = (const unsigned char *)username; *p; p++) {
    if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
           (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.' ))
      return false;
  }
  return true;
}

int auth_verify_user(const char *username, const char *password,
                     char *error, size_t error_size) {
  if (!safe_username(username) || !password) {
    set_error(error, error_size, "invalid credentials");
    return -EINVAL;
  }
  struct passwd *entry = getpwnam(username);
  if (!entry || !user_is_admin(entry)) {
    set_error(error, error_size, "invalid credentials or account is not authorized for switch management");
    return -EACCES;
  }

  struct pam_secret secret = {username, password};
  struct pam_conv conv = {conversation, &secret};
  pam_handle_t *handle = NULL;
  int rc = pam_start("configd", username, &conv, &handle);
  if (rc == PAM_SUCCESS) rc = pam_authenticate(handle, PAM_SILENT);
  if (rc == PAM_SUCCESS) rc = pam_acct_mgmt(handle, PAM_SILENT);
  if (handle) pam_end(handle, rc);
  if (rc != PAM_SUCCESS) {
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
    if (!entry->pw_name || !safe_username(entry->pw_name)) continue;
    const char *shell = entry->pw_shell ? entry->pw_shell : "";
    if (strstr(shell, "nologin") || strstr(shell, "false")) continue;
    if (!user_is_admin(entry)) continue;
    struct json_object *user = json_object_new_object();
    json_object_object_add(user, "username",
                           json_object_new_string(entry->pw_name));
    json_object_object_add(user, "uid",
                           json_object_new_int64((int64_t)entry->pw_uid));
    json_object_object_add(user, "home",
                           json_object_new_string(entry->pw_dir ? entry->pw_dir : ""));
    json_object_object_add(user, "shell",
                           json_object_new_string(shell));
    json_object_array_add(users, user);
  }
  endpwent();
  return users;
}

static int run_passwd(const char *username, const char *new_password,
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
    const char *program = access("/usr/bin/passwd", X_OK) == 0
                              ? "/usr/bin/passwd" : "/bin/passwd";
    execl(program, "passwd", "-a", "sha256", username, (char *)NULL);
    _exit(127);
  }

  close(input_pipe[0]);
  dprintf(input_pipe[1], "%s\n%s\n", new_password, new_password);
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
  if (!safe_username(actor) || !safe_username(target) || !getpwnam(target)) {
    set_error(error, error_size, "unknown user");
    return -ENOENT;
  }
  if (!new_password || strlen(new_password) < 8 || strlen(new_password) > 128 ||
      strchr(new_password, '\n') || strchr(new_password, '\r')) {
    set_error(error, error_size,
              "new password must be between 8 and 128 characters");
    return -EINVAL;
  }
  int rc = auth_verify_user(actor, actor_password, error, error_size);
  if (rc != 0) return rc;
  struct passwd *actor_entry = getpwnam(actor);
  if (!actor_entry) return -ENOENT;
  if (actor_entry->pw_uid != 0 && strcmp(actor, target) != 0) {
    set_error(error, error_size, "only root may change another account");
    return -EACCES;
  }
  return run_passwd(target, new_password, error, error_size);
}
