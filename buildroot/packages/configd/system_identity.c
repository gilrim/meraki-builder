#define _GNU_SOURCE
#include "system_identity.h"
#include "service_ops.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SYSTEM_POLICY_PATH "/config/postmerkos/system.json"
#define SYSTEM_POLICY_DEFAULT "/usr/share/postmerkos/defaults/system.json"
#define DEFAULT_HOSTNAME "postmerkos"

static const char *env_or_default(const char *name, const char *fallback) {
  const char *value = getenv(name);
  return value && *value ? value : fallback;
}

static void set_error(char *error, size_t size, const char *message) {
  if (error && size) snprintf(error, size, "%s", message ? message : "error");
}

static struct json_object *member(struct json_object *object, const char *key) {
  struct json_object *value = NULL;
  if (!object || !json_object_is_type(object, json_type_object) ||
      !json_object_object_get_ex(object, key, &value)) return NULL;
  return value;
}

static int ensure_parent(void) {
  mkdir("/config", 0755);
  mkdir("/config/postmerkos", 0700);
  return 0;
}

static int atomic_json_write(const char *path, struct json_object *object) {
  char temp[320];
  if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp))
    return -ENAMETOOLONG;
  int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return -errno;
  const char *text = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PRETTY);
  size_t left = strlen(text); const char *cursor = text; int rc = 0;
  while (left) {
    ssize_t wrote = write(fd, cursor, left);
    if (wrote < 0) { if (errno == EINTR) continue; rc = -errno; break; }
    cursor += wrote; left -= (size_t)wrote;
  }
  if (!rc && write(fd, "\n", 1) != 1) rc = -EIO;
  if (!rc && fsync(fd) != 0) rc = -errno;
  if (close(fd) != 0 && !rc) rc = -errno;
  if (!rc && rename(temp, path) != 0) rc = -errno;
  if (rc) unlink(temp);
  return rc;
}

int system_identity_validate_hostname(const char *hostname, char *error,
                                      size_t error_size) {
  if (!hostname || !*hostname) {
    set_error(error, error_size, "hostname is required"); return -EINVAL;
  }
  size_t length = strlen(hostname);
  if (length > 63) {
    set_error(error, error_size, "hostname must be 63 characters or fewer"); return -EINVAL;
  }
  if (hostname[0] == '-' || hostname[length - 1] == '-') {
    set_error(error, error_size, "hostname cannot begin or end with a hyphen"); return -EINVAL;
  }
  if (!strcmp(hostname, "localhost") || !strcmp(hostname, "local")) {
    set_error(error, error_size, "hostname is reserved"); return -EINVAL;
  }
  for (const unsigned char *p = (const unsigned char *)hostname; *p; ++p) {
    if (!(islower(*p) || isdigit(*p) || *p == '-')) {
      set_error(error, error_size,
                "hostname may contain only lowercase letters, digits, and hyphens");
      return -EINVAL;
    }
  }
  return 0;
}

struct json_object *system_identity_policy_load(void) {
  struct json_object *policy = json_object_from_file(
      env_or_default("CONFIGD_SYSTEM_POLICY", SYSTEM_POLICY_PATH));
  if (!policy) policy = json_object_from_file(
      env_or_default("CONFIGD_SYSTEM_POLICY_DEFAULT", SYSTEM_POLICY_DEFAULT));
  if (!policy) {
    policy = json_object_new_object();
    json_object_object_add(policy, "hostname", json_object_new_string(DEFAULT_HOSTNAME));
  }
  return policy;
}

static int normalize_hostname(const char *input, char output[64],
                              char *error, size_t error_size) {
  if (!input) { set_error(error, error_size, "hostname is required"); return -EINVAL; }
  size_t length = strlen(input);
  if (length >= 64) { set_error(error, error_size, "hostname must be 63 characters or fewer"); return -EINVAL; }
  for (size_t i = 0; i < length; i++)
    output[i] = (char)tolower((unsigned char)input[i]);
  output[length] = '\0';
  return system_identity_validate_hostname(output, error, error_size);
}

static const char *policy_hostname(struct json_object *policy) {
  struct json_object *value = member(policy, "hostname");
  if (!value || !json_object_is_type(value, json_type_string)) return DEFAULT_HOSTNAME;
  const char *hostname = json_object_get_string(value);
  return hostname && *hostname ? hostname : DEFAULT_HOSTNAME;
}

static int write_text(const char *path, const char *text, mode_t mode) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0) return -errno;
  size_t left = strlen(text); const char *cursor = text; int rc = 0;
  while (left) {
    ssize_t wrote = write(fd, cursor, left);
    if (wrote < 0) { if (errno == EINTR) continue; rc = -errno; break; }
    cursor += wrote; left -= (size_t)wrote;
  }
  if (!rc && fsync(fd) != 0) rc = -errno;
  if (close(fd) != 0 && !rc) rc = -errno;
  return rc;
}

static int render_avahi(const char *hostname) {
  const char *path = env_or_default("CONFIGD_AVAHI_CONF", "/etc/avahi/avahi-daemon.conf");
  char text[1024];
  int written = snprintf(text, sizeof(text),
      "[server]\n"
      "host-name=%s\n"
      "domain-name=local\n"
      "use-ipv4=yes\n"
      "use-ipv6=no\n"
      "allow-interfaces=linux_mgmt\n"
      "enable-dbus=no\n\n"
      "[wide-area]\n"
      "enable-wide-area=no\n\n"
      "[publish]\n"
      "publish-addresses=yes\n"
      "publish-hinfo=no\n"
      "publish-workstation=no\n"
      "publish-domain=no\n",
      hostname);
  if (written < 0 || (size_t)written >= sizeof(text)) return -ENAMETOOLONG;
  return write_text(path, text, 0644);
}

static int apply_policy(struct json_object *policy, char *error, size_t error_size) {
  const char *hostname = policy_hostname(policy);
  int rc = system_identity_validate_hostname(hostname, error, error_size);
  if (rc != 0) return rc;
  if (!getenv("CONFIGD_HOSTNAME_DRY_RUN") && sethostname(hostname, strlen(hostname)) != 0) {
    set_error(error, error_size, strerror(errno)); return -errno;
  }
  if (!getenv("CONFIGD_HOSTNAME_DRY_RUN")) {
    rc = write_text(env_or_default("CONFIGD_HOSTNAME_FILE", "/etc/hostname"), hostname, 0644);
    if (rc != 0) { set_error(error, error_size, strerror(-rc)); return rc; }
    rc = render_avahi(hostname);
    if (rc != 0) { set_error(error, error_size, strerror(-rc)); return rc; }
  }
  return 0;
}

int system_identity_apply(char *error, size_t error_size) {
  struct json_object *policy = system_identity_policy_load();
  if (!policy) { set_error(error, error_size, "system identity policy unavailable"); return -ENOENT; }
  int rc = apply_policy(policy, error, error_size);
  json_object_put(policy);
  return rc;
}

int system_identity_save(struct json_object *policy, char *error, size_t error_size) {
  if (!policy || !json_object_is_type(policy, json_type_object)) {
    set_error(error, error_size, "system identity policy must be an object"); return -EINVAL;
  }
  char hostname[64];
  int rc = normalize_hostname(policy_hostname(policy), hostname, error, error_size);
  if (rc != 0) return rc;
  struct json_object *candidate = json_tokener_parse(
      json_object_to_json_string_ext(policy, JSON_C_TO_STRING_PLAIN));
  if (!candidate) { set_error(error, error_size, "unable to copy system identity policy"); return -ENOMEM; }
  json_object_object_add(candidate, "hostname", json_object_new_string(hostname));
  struct json_object *previous = system_identity_policy_load();
  rc = apply_policy(candidate, error, error_size);
  if (rc != 0) { if (previous) json_object_put(previous); json_object_put(candidate); return rc; }
  if (!getenv("CONFIGD_HOSTNAME_DRY_RUN")) {
    char service_error[128] = {0};
    rc = service_reconfigure("mdns", service_error, sizeof(service_error));
    if (rc != 0) {
      char rollback[128] = {0};
      if (previous) {
        apply_policy(previous, rollback, sizeof(rollback));
        service_reconfigure("mdns", rollback, sizeof(rollback));
      }
      set_error(error, error_size, service_error[0] ? service_error : "mDNS reconfiguration failed");
      if (previous) json_object_put(previous);
      json_object_put(candidate);
      return rc;
    }
  }
  ensure_parent();
  const char *path = env_or_default("CONFIGD_SYSTEM_POLICY", SYSTEM_POLICY_PATH);
  rc = atomic_json_write(path, candidate);
  if (rc != 0) {
    char rollback[128] = {0};
    if (previous) {
      apply_policy(previous, rollback, sizeof(rollback));
      if (!getenv("CONFIGD_HOSTNAME_DRY_RUN"))
        service_reconfigure("mdns", rollback, sizeof(rollback));
    }
    set_error(error, error_size, strerror(-rc));
  }
  if (previous) json_object_put(previous);
  json_object_put(candidate);
  return rc;
}

struct json_object *system_identity_status_json(void) {
  struct json_object *root = json_object_new_object();
  struct json_object *policy = system_identity_policy_load();
  const char *configured = policy_hostname(policy);
  char effective[256] = {0};
  if (gethostname(effective, sizeof(effective) - 1) != 0)
    snprintf(effective, sizeof(effective), "%s", configured);
  char advertised[320];
  snprintf(advertised, sizeof(advertised), "%s.local", effective);
  json_object_object_add(root, "configured_hostname", json_object_new_string(configured));
  json_object_object_add(root, "effective_hostname", json_object_new_string(effective));
  json_object_object_add(root, "advertised_name", json_object_new_string(advertised));
  json_object_object_add(root, "mdns_management_only", json_object_new_boolean(true));
  json_object_object_add(root, "dhcp_hostname_registration_supported", json_object_new_boolean(false));
  json_object_object_add(root, "conflict_state", json_object_new_string("unobserved"));
  json_object_object_add(root, "advertised_name_verified", json_object_new_boolean(false));
  if (policy) json_object_put(policy);
  return root;
}
