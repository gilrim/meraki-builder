#include "config_file.h"
#include "configd.h"
#include <libpostmerkos.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char runtime_error[256];

static void fsync_parent_directory(const char *path) {
  if (!path || !*path) return;
  char directory[512];
  int length = snprintf(directory, sizeof(directory), "%s", path);
  if (length < 0 || (size_t)length >= sizeof(directory)) return;
  char *slash = strrchr(directory, '/');
  if (!slash) {
    snprintf(directory, sizeof(directory), ".");
  } else if (slash == directory) {
    slash[1] = '\0';
  } else {
    *slash = '\0';
  }
  int fd = open(directory, O_RDONLY);
  if (fd < 0) return;
  if (fsync(fd) != 0 && errno != EINVAL && errno != EROFS)
    fprintf(stderr, "%s config: warning: directory fsync failed: %s\n",
            get_time(), strerror(errno));
  close(fd);
}

static int copy_file(const char *source, const char *destination) {
  int in = open(source, O_RDONLY);
  if (in < 0) return -errno;
  int out = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (out < 0) {
    int rc = -errno;
    close(in);
    return rc;
  }
  char buffer[4096];
  int rc = 0;
  for (;;) {
    ssize_t got = read(in, buffer, sizeof(buffer));
    if (got == 0) break;
    if (got < 0) {
      if (errno == EINTR) continue;
      rc = -errno;
      break;
    }
    char *cursor = buffer;
    ssize_t left = got;
    while (left > 0) {
      ssize_t wrote = write(out, cursor, (size_t)left);
      if (wrote < 0) {
        if (errno == EINTR) continue;
        rc = -errno;
        break;
      }
      cursor += wrote;
      left -= wrote;
    }
    if (rc != 0) break;
  }
  if (rc == 0 && fsync(out) != 0) rc = -errno;
  close(in);
  if (close(out) != 0 && rc == 0) rc = -errno;
  if (rc != 0) unlink(destination);
  return rc;
}

struct json_object *load_json_file(const char *path) {
  return path ? json_object_from_file(path) : NULL;
}

struct json_object *load_config_file(void) {
  return load_json_file(config_file);
}

static void set_error(char *error, size_t size, const char *message) {
  if (error && size) snprintf(error, size, "%s", message ? message : "error");
}

int save_config_file(struct json_object *json, char *error, size_t error_size) {
  if (!json) {
    set_error(error, error_size, "configuration is null");
    return -EINVAL;
  }
  if (dry_run) return 0;

  char temp_path[512];
  int len = snprintf(temp_path, sizeof(temp_path), "%s.tmp", config_file);
  if (len < 0 || (size_t)len >= sizeof(temp_path)) {
    set_error(error, error_size, "configuration path is too long");
    return -ENAMETOOLONG;
  }

  int fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) {
    set_error(error, error_size, strerror(errno));
    return -errno;
  }

  const char *text = json_object_to_json_string_ext(
      json, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
  size_t remaining = strlen(text);
  const char *cursor = text;
  int rc = 0;
  while (remaining) {
    ssize_t written = write(fd, cursor, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      rc = -errno;
      break;
    }
    cursor += written;
    remaining -= (size_t)written;
  }
  if (rc == 0 && write(fd, "\n", 1) != 1) rc = -EIO;
  if (rc == 0 && fsync(fd) != 0) rc = -errno;
  if (close(fd) != 0 && rc == 0) rc = -errno;

  if (rc == 0 && access(config_file, F_OK) == 0) {
    char backup_path[512];
    int backup_len = snprintf(backup_path, sizeof(backup_path), "%s.bak", config_file);
    if (backup_len > 0 && (size_t)backup_len < sizeof(backup_path)) {
      int backup_rc = copy_file(config_file, backup_path);
      if (backup_rc != 0)
        fprintf(stderr, "%s config: warning: backup failed: %s\n",
                get_time(), strerror(-backup_rc));
    }
  }
  if (rc == 0 && rename(temp_path, config_file) != 0) rc = -errno;
  if (rc == 0) fsync_parent_directory(config_file);
  if (rc != 0) {
    unlink(temp_path);
    set_error(error, error_size, strerror(-rc));
    return rc;
  }

  printf("%s config: atomically written to %s\n", get_time(), config_file);
  return 0;
}

int config_file_mtime(long long *mtime_ns) {
  if (!mtime_ns) return -EINVAL;
  struct stat st;
  if (stat(config_file, &st) != 0) return -errno;
#if defined(__linux__)
  *mtime_ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#else
  *mtime_ns = (long long)st.st_mtime * 1000000000LL;
#endif
  return 0;
}

void config_file_set_runtime_error(const char *message) {
  snprintf(runtime_error, sizeof(runtime_error), "%s", message ? message : "");
}

const char *config_file_runtime_error(void) {
  return runtime_error[0] ? runtime_error : NULL;
}
