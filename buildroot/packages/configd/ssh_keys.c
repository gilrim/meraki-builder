#include "ssh_keys.h"
#include "config_file.h"
#include "json_util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SSH_KEY_MAX_LEN 16384
#define SSH_BLOB_MAX_LEN 12288

static void set_err(char *error, size_t n, const char *msg) {
  if (error && n) snprintf(error, n, "%s", msg ? msg : "error");
}

static const char *ALLOWED_TYPES[] = {
  "ssh-ed25519", "ssh-rsa",
  "ecdsa-sha2-nistp256", "ecdsa-sha2-nistp384", "ecdsa-sha2-nistp521",
  "sk-ssh-ed25519@openssh.com", "sk-ecdsa-sha2-nistp256@openssh.com", NULL
};

static bool token(const char **pp, char *out, size_t n) {
  const char *p = *pp;
  while (*p == ' ' || *p == '\t') p++;
  const char *start = p;
  while (*p && *p != ' ' && *p != '\t') p++;
  size_t len = (size_t)(p - start);
  if (len == 0 || len >= n) { *pp = p; return false; }
  memcpy(out, start, len); out[len] = '\0'; *pp = p; return true;
}

static bool type_allowed(const char *type) {
  for (size_t i = 0; ALLOWED_TYPES[i]; i++) if (!strcmp(type, ALLOWED_TYPES[i])) return true;
  return false;
}

static int b64_value(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static int decode_base64(const char *text, unsigned char *out, size_t cap,
                         size_t *out_len) {
  size_t len = strlen(text);
  if (!len || len % 4 == 1) return -EINVAL;
  size_t used = 0, padding = 0;
  unsigned int accumulator = 0;
  int bits = 0;
  bool saw_padding = false;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c == '=') {
      saw_padding = true;
      if (++padding > 2) return -EINVAL;
      continue;
    }
    if (saw_padding) return -EINVAL;
    int value = b64_value(c);
    if (value < 0) return -EINVAL;
    accumulator = (accumulator << 6) | (unsigned int)value;
    bits += 6;
    while (bits >= 8) {
      bits -= 8;
      if (used >= cap) return -E2BIG;
      out[used++] = (unsigned char)((accumulator >> bits) & 0xffU);
    }
  }
  if (padding && len % 4 != 0) return -EINVAL;
  if ((padding == 1 && bits != 2) || (padding == 2 && bits != 4)) return -EINVAL;
  if (bits && (accumulator & ((1U << bits) - 1U)) != 0) return -EINVAL;
  *out_len = used;
  return 0;
}

static int read_u32(const unsigned char **cursor, size_t *remaining, uint32_t *value) {
  if (*remaining < 4) return -EINVAL;
  const unsigned char *p = *cursor;
  *value = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
  *cursor += 4; *remaining -= 4; return 0;
}

static int read_string(const unsigned char **cursor, size_t *remaining,
                       const unsigned char **data, size_t *length) {
  uint32_t count = 0;
  if (read_u32(cursor, remaining, &count) != 0 || count > *remaining) return -EINVAL;
  *data = *cursor; *length = count; *cursor += count; *remaining -= count; return 0;
}

static bool bytes_equal_text(const unsigned char *data, size_t length, const char *text) {
  return strlen(text) == length && !memcmp(data, text, length);
}

static int validate_ssh_blob(const char *outer_type, const unsigned char *blob,
                             size_t blob_len, char *error, size_t n) {
  const unsigned char *cursor = blob, *field = NULL;
  size_t remaining = blob_len, field_len = 0;
  if (read_string(&cursor, &remaining, &field, &field_len) != 0 ||
      !bytes_equal_text(field, field_len, outer_type)) {
    set_err(error, n, "embedded key type does not match the public-key type"); return -EINVAL;
  }
  if (!strcmp(outer_type, "ssh-ed25519")) {
    if (read_string(&cursor, &remaining, &field, &field_len) != 0 || field_len != 32) goto malformed;
  } else if (!strcmp(outer_type, "ssh-rsa")) {
    const unsigned char *e = NULL, *modulus = NULL; size_t e_len = 0, modulus_len = 0;
    if (read_string(&cursor, &remaining, &e, &e_len) != 0 || !e_len ||
        read_string(&cursor, &remaining, &modulus, &modulus_len) != 0 || modulus_len < 32) goto malformed;
    (void)e; (void)modulus;
  } else if (!strncmp(outer_type, "ecdsa-sha2-", 11)) {
    const char *curve_name = outer_type + 11;
    const unsigned char *curve = NULL, *point = NULL; size_t curve_len = 0, point_len = 0;
    size_t expected = !strcmp(curve_name, "nistp256") ? 65U :
                      !strcmp(curve_name, "nistp384") ? 97U :
                      !strcmp(curve_name, "nistp521") ? 133U : 0U;
    if (!expected || read_string(&cursor, &remaining, &curve, &curve_len) != 0 ||
        !bytes_equal_text(curve, curve_len, curve_name) ||
        read_string(&cursor, &remaining, &point, &point_len) != 0 ||
        point_len != expected || point[0] != 0x04) goto malformed;
  } else if (!strcmp(outer_type, "sk-ssh-ed25519@openssh.com")) {
    const unsigned char *application = NULL; size_t application_len = 0;
    if (read_string(&cursor, &remaining, &field, &field_len) != 0 || field_len != 32 ||
        read_string(&cursor, &remaining, &application, &application_len) != 0 || !application_len) goto malformed;
  } else if (!strcmp(outer_type, "sk-ecdsa-sha2-nistp256@openssh.com")) {
    const unsigned char *curve = NULL, *point = NULL, *application = NULL;
    size_t curve_len = 0, point_len = 0, application_len = 0;
    if (read_string(&cursor, &remaining, &curve, &curve_len) != 0 ||
        !bytes_equal_text(curve, curve_len, "nistp256") ||
        read_string(&cursor, &remaining, &point, &point_len) != 0 || point_len != 65 || point[0] != 0x04 ||
        read_string(&cursor, &remaining, &application, &application_len) != 0 || !application_len) goto malformed;
  } else goto malformed;
  if (remaining) { set_err(error, n, "key data contains trailing fields"); return -EINVAL; }
  return 0;
malformed:
  set_err(error, n, "key data is truncated or has an invalid SSH wire format"); return -EINVAL;
}

int ssh_key_validate(const char *line, char *error, size_t n) {
  if (!line || !*line) { set_err(error, n, "key is empty"); return -EINVAL; }
  if (strpbrk(line, "\r\n")) { set_err(error, n, "key must be a single line"); return -EINVAL; }
  if (strlen(line) > SSH_KEY_MAX_LEN) { set_err(error, n, "key is too long"); return -EINVAL; }
  const char *cursor = line;
  char type[80], encoded[SSH_KEY_MAX_LEN];
  if (!token(&cursor, type, sizeof(type))) { set_err(error, n, "key type is missing"); return -EINVAL; }
  if (!type_allowed(type)) { set_err(error, n, "unsupported or option-prefixed key type"); return -EINVAL; }
  if (!token(&cursor, encoded, sizeof(encoded))) { set_err(error, n, "key data is missing"); return -EINVAL; }
  for (const char *c = cursor; *c; c++) if (iscntrl((unsigned char)*c)) {
    set_err(error, n, "comment contains control characters"); return -EINVAL;
  }
  unsigned char *decoded = malloc(SSH_BLOB_MAX_LEN);
  if (!decoded) { set_err(error, n, "out of memory"); return -ENOMEM; }
  size_t decoded_len = 0;
  int rc = decode_base64(encoded, decoded, SSH_BLOB_MAX_LEN, &decoded_len);
  if (rc != 0) { free(decoded); set_err(error, n, "key data is not valid canonical base64"); return -EINVAL; }
  rc = validate_ssh_blob(type, decoded, decoded_len, error, n);
  free(decoded); return rc;
}

int ssh_key_identity(const char *line, char *out, size_t n) {
  char error[128];
  if (ssh_key_validate(line, error, sizeof(error)) != 0) return -EINVAL;
  const char *cursor = line; char type[80], blob[SSH_KEY_MAX_LEN];
  if (!token(&cursor, type, sizeof(type)) || !token(&cursor, blob, sizeof(blob))) return -EINVAL;
  return (size_t)snprintf(out, n, "%s %s", type, blob) < n ? 0 : -EINVAL;
}

static struct json_object *keys_array(struct json_object *cfg, bool create) {
  struct json_object *ssh = NULL, *arr = NULL;
  if (!json_object_object_get_ex(cfg, "ssh", &ssh)) {
    if (!create) return NULL;
    ssh = json_object_new_object(); json_object_object_add(cfg, "ssh", ssh);
  }
  if (!json_object_object_get_ex(ssh, "authorized_keys", &arr)) {
    if (!create) return NULL;
    arr = json_object_new_array(); json_object_object_add(ssh, "authorized_keys", arr);
  }
  return arr;
}

struct json_object *ssh_keys_list(void) {
  struct json_object *out = json_object_new_array(), *cfg = load_config_file();
  if (!cfg) return out;
  struct json_object *arr = keys_array(cfg, false);
  for (size_t i = 0; arr && i < json_object_array_length(arr); i++) {
    struct json_object *entry = json_object_array_get_idx(arr, i), *key = NULL, *label = NULL;
    if (!json_object_object_get_ex(entry, "key", &key)) continue;
    struct json_object *row = json_object_new_object();
    json_object_object_add(row, "label", json_object_new_string(
      json_object_object_get_ex(entry, "label", &label) ? json_object_get_string(label) : ""));
    json_object_object_add(row, "key", json_object_new_string(json_object_get_string(key)));
    json_object_array_add(out, row);
  }
  json_object_put(cfg); return out;
}

static void fsync_parent_dir(const char *path) {
  char dir[640];
  if ((size_t)snprintf(dir, sizeof(dir), "%s", path) >= sizeof(dir)) return;
  char *slash = strrchr(dir, '/'); if (!slash) return;
  if (slash == dir) slash[1] = '\0'; else *slash = '\0';
  int fd = open(dir, O_RDONLY | O_DIRECTORY); if (fd < 0) return;
  (void)fsync(fd); close(fd);
}

static int write_atomic(const char *path, const char *text, mode_t mode) {
  if (getenv("CONFIGD_SSH_FAIL_RENDER")) return -EIO;
  char temp[640];
  if ((size_t)snprintf(temp, sizeof(temp), "%s.tmp.%ld", path, (long)getpid()) >= sizeof(temp)) return -ENAMETOOLONG;
  int fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, mode); if (fd < 0) return -errno;
  int rc = 0; size_t remaining = strlen(text); const char *cursor = text;
  while (remaining) {
    ssize_t written = write(fd, cursor, remaining);
    if (written < 0) { if (errno == EINTR) continue; rc = -errno; break; }
    cursor += written; remaining -= (size_t)written;
  }
  if (!rc && fsync(fd) != 0) rc = -errno;
  if (close(fd) != 0 && !rc) rc = -errno;
  if (!rc && rename(temp, path) != 0) rc = -errno;
  if (!rc) fsync_parent_dir(path); else unlink(temp);
  return rc;
}

static int render_config(struct json_object *cfg, char *error, size_t n) {
  const char *dir = getenv("CONFIGD_SSH_DIR"); if (!dir || !*dir) dir = "/root/.ssh";
  const char *file = getenv("CONFIGD_AUTHORIZED_KEYS"); if (!file || !*file) file = "/root/.ssh/authorized_keys";
  if (mkdir(dir, 0700) != 0 && errno != EEXIST) { set_err(error, n, strerror(errno)); return -errno; }
  (void)chmod(dir, 0700);
  size_t cap = 4096, length = 0; char *body = malloc(cap);
  if (!body) { set_err(error, n, "out of memory"); return -ENOMEM; }
  body[0] = '\0';
  struct json_object *arr = cfg ? keys_array(cfg, false) : NULL;
  for (size_t i = 0; arr && i < json_object_array_length(arr); i++) {
    struct json_object *entry = json_object_array_get_idx(arr, i), *key = NULL;
    if (!json_object_object_get_ex(entry, "key", &key)) continue;
    const char *text = json_object_get_string(key); char validation[160];
    if (ssh_key_validate(text, validation, sizeof(validation)) != 0) {
      free(body); set_err(error, n, validation); return -EINVAL;
    }
    size_t key_len = strlen(text), need = length + key_len + 2;
    if (need > cap) {
      while (cap < need) cap *= 2;
      char *expanded = realloc(body, cap);
      if (!expanded) { free(body); set_err(error, n, "out of memory"); return -ENOMEM; }
      body = expanded;
    }
    memcpy(body + length, text, key_len); length += key_len;
    body[length++] = '\n'; body[length] = '\0';
  }
  int rc = write_atomic(file, body, 0600);
  free(body);
  if (rc != 0) set_err(error, n, strerror(-rc));
  return rc;
}

int ssh_keys_render(char *error, size_t n) {
  struct json_object *cfg = load_config_file();
  int rc = render_config(cfg, error, n); if (cfg) json_object_put(cfg); return rc;
}

static void rollback_config_and_keys(struct json_object *old_cfg) {
  char ignored[256] = {0};
  (void)save_config_file(old_cfg, ignored, sizeof(ignored));
  (void)render_config(old_cfg, ignored, sizeof(ignored));
}

int ssh_keys_add(const char *label, const char *key, char *error, size_t n) {
  int rc = ssh_key_validate(key, error, n); if (rc != 0) return rc;
  char identity[SSH_KEY_MAX_LEN];
  if (ssh_key_identity(key, identity, sizeof(identity)) != 0) { set_err(error, n, "key could not be normalized"); return -EINVAL; }
  struct json_object *old_cfg = load_config_file(); if (!old_cfg) old_cfg = json_object_new_object();
  struct json_object *cfg = json_deep_copy_object(old_cfg);
  if (!cfg) { json_object_put(old_cfg); set_err(error, n, "out of memory"); return -ENOMEM; }
  struct json_object *arr = keys_array(cfg, true);
  for (size_t i = 0; i < json_object_array_length(arr); i++) {
    struct json_object *entry = json_object_array_get_idx(arr, i), *stored = NULL; char stored_identity[SSH_KEY_MAX_LEN];
    if (json_object_object_get_ex(entry, "key", &stored) &&
        ssh_key_identity(json_object_get_string(stored), stored_identity, sizeof(stored_identity)) == 0 &&
        !strcmp(identity, stored_identity)) {
      json_object_put(cfg); json_object_put(old_cfg); set_err(error, n, "an identical key is already authorized"); return -EEXIST;
    }
  }
  struct json_object *entry = json_object_new_object();
  json_object_object_add(entry, "label", json_object_new_string(label ? label : ""));
  json_object_object_add(entry, "key", json_object_new_string(key)); json_object_array_add(arr, entry);
  rc = save_config_file(cfg, error, n);
  if (!rc) rc = render_config(cfg, error, n);
  if (rc) rollback_config_and_keys(old_cfg);
  json_object_put(cfg); json_object_put(old_cfg); return rc;
}

int ssh_keys_remove(const char *key, char *error, size_t n) {
  if (!key || !*key) { set_err(error, n, "key is required"); return -EINVAL; }
  struct json_object *old_cfg = load_config_file(); if (!old_cfg) old_cfg = json_object_new_object();
  struct json_object *cfg = json_deep_copy_object(old_cfg);
  if (!cfg) { json_object_put(old_cfg); set_err(error, n, "out of memory"); return -ENOMEM; }
  struct json_object *arr = keys_array(cfg, false); bool removed = false;
  for (size_t i = 0; arr && i < json_object_array_length(arr); i++) {
    struct json_object *entry = json_object_array_get_idx(arr, i), *stored = NULL;
    if (json_object_object_get_ex(entry, "key", &stored) && !strcmp(json_object_get_string(stored), key)) {
      json_object_array_del_idx(arr, i, 1); removed = true; break;
    }
  }
  if (!removed) { json_object_put(cfg); json_object_put(old_cfg); set_err(error, n, "key not found"); return -ENOENT; }
  int rc = render_config(cfg, error, n);
  if (!rc) rc = save_config_file(cfg, error, n);
  if (rc) rollback_config_and_keys(old_cfg);
  json_object_put(cfg); json_object_put(old_cfg); return rc;
}
