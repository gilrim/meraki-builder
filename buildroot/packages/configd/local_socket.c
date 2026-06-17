#define _GNU_SOURCE
#include "local_socket.h"
#include "config_file.h"
#include "console_cli.h"
#include "roles.h"
#include "status.h"

#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <poll.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define LOCAL_REQUEST_MAX 65536

static int send_json(int fd, const char *type, struct json_object *data) {
  struct json_object *reply = json_object_new_object();
  json_object_object_add(reply, "type", json_object_new_string(type));
  json_object_object_add(reply, "data", data ? json_object_get(data) : json_object_new_null());
  const char *text = json_object_to_json_string_ext(reply, JSON_C_TO_STRING_PLAIN);
  size_t left = strlen(text);
  const char *cursor = text;
  int rc = 0;
  while (left) {
    ssize_t wrote = write(fd, cursor, left);
    if (wrote < 0) { if (errno == EINTR) continue; rc = -errno; break; }
    cursor += wrote; left -= (size_t)wrote;
  }
  if (rc == 0 && write(fd, "\n", 1) != 1) rc = -EIO;
  json_object_put(reply);
  return rc;
}

static int send_error(int fd, int status, const char *message) {
  struct json_object *data = json_object_new_object();
  json_object_object_add(data, "status", json_object_new_int(status));
  json_object_object_add(data, "message", json_object_new_string(message ? message : "error"));
  int rc = send_json(fd, "error", data);
  json_object_put(data);
  return rc;
}

static const char *request_type(struct json_object *request) {
  struct json_object *type = NULL;
  return request && json_object_object_get_ex(request, "type", &type) &&
      json_object_is_type(type, json_type_string) ? json_object_get_string(type) : NULL;
}

static void handle_client(int fd) {
  struct ucred credentials;
  socklen_t credentials_length = sizeof(credentials);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &credentials_length) != 0) {
    send_error(fd, 500, "unable to identify local client"); return;
  }
  struct passwd *passwd = getpwuid(credentials.uid);
  const char *username = passwd && passwd->pw_name ? passwd->pw_name : "";
  enum postmerkos_role role = role_for_username(username);
  if (role == POSTMERKOS_ROLE_NONE) { send_error(fd, 403, "account has no management role"); return; }

  char buffer[LOCAL_REQUEST_MAX + 1];
  ssize_t got = read(fd, buffer, LOCAL_REQUEST_MAX);
  if (got <= 0) return;
  buffer[got] = '\0';
  struct json_object *request = json_tokener_parse(buffer);
  const char *type = request_type(request);
  if (!request || !type) { send_error(fd, 400, "malformed request"); goto done; }

  if (!strcmp(type, "ping")) {
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "message", json_object_new_string("pong"));
    send_json(fd, "ack", data); json_object_put(data);
  } else if (!strcmp(type, "session")) {
    struct json_object *identity = role_identity_json(username);
    send_json(fd, "session", identity); json_object_put(identity);
  } else if (!strcmp(type, "status.get") && role_has_capability(role, "status.read")) {
    struct json_object *status = get_status();
    send_json(fd, "status", status); json_object_put(status);
  } else if (!strcmp(type, "config.get") && role_has_capability(role, "config.read")) {
    struct json_object *config = load_config_file();
    if (!config) send_error(fd, 404, "configuration unavailable");
    else { send_json(fd, "config", config); json_object_put(config); }
  } else if (!strcmp(type, "config.path.get") && role_has_capability(role, "config.read")) {
    struct json_object *data = NULL, *path = NULL;
    json_object_object_get_ex(request, "data", &data);
    if (!data || !json_object_object_get_ex(data, "path", &path) ||
        !json_object_is_type(path, json_type_string)) send_error(fd, 400, "path is required");
    else {
      struct json_object *config = load_config_file();
      struct json_object *value = config ? console_path_get(config, json_object_get_string(path)) : NULL;
      if (!value) send_error(fd, 404, "configuration path not found");
      else send_json(fd, "value", value);
      if (config) json_object_put(config);
    }
  } else send_error(fd, 403, "operation is unavailable or not permitted");

done:
  if (request) json_object_put(request);
}

int local_socket_init(const char *path) {
  if (!path || !*path) return -EINVAL;
  char directory[256];
  snprintf(directory, sizeof(directory), "%s", path);
  char *slash = strrchr(directory, '/');
  if (slash) { *slash = '\0'; if (*directory) mkdir(directory, 0755); }
  unlink(path);
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -errno;
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", path) >= (int)sizeof(address.sun_path)) {
    close(fd); return -ENAMETOOLONG;
  }
  if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(fd, 8) != 0) {
    int rc = -errno; close(fd); unlink(path); return rc;
  }
  chmod(path, 0666);
  return fd;
}

int local_socket_service_once(int listen_fd, int timeout_ms) {
  struct pollfd pollfd = { .fd = listen_fd, .events = POLLIN };
  int ready = poll(&pollfd, 1, timeout_ms);
  if (ready < 0) return errno == EINTR ? 0 : -errno;
  if (ready == 0 || !(pollfd.revents & POLLIN)) return 0;
  int client = accept(listen_fd, NULL, NULL);
  if (client < 0) return errno == EINTR ? 0 : -errno;
  handle_client(client);
  close(client);
  return 1;
}

void local_socket_shutdown(int listen_fd, const char *path) {
  if (listen_fd >= 0) close(listen_fd);
  if (path) unlink(path);
}
