#include "libpostmerkos.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *get_time(void) {
  time_t now = time(NULL);
  struct tm tm_now;
  static char buffer[32];

  if (!gmtime_r(&now, &tm_now)) {
    snprintf(buffer, sizeof(buffer), "1970-01-01T00:00:00Z");
    return buffer;
  }
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_now);
  return buffer;
}

bool starts_with(const char *str, const char *prefix) {
  if (!str || !prefix) return false;
  size_t prefix_len = strlen(prefix);
  return strlen(str) >= prefix_len && strncmp(str, prefix, prefix_len) == 0;
}

bool ends_with(const char *str, const char *suffix) {
  if (!str || !suffix) return false;
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  return str_len >= suffix_len &&
         strcmp(str + str_len - suffix_len, suffix) == 0;
}

char *itoa(int num, char *buffer, int base) {
  if (!buffer || base < 2 || base > 10) return NULL;
  if (base == 10) {
    snprintf(buffer, 32, "%d", num);
    return buffer;
  }

  unsigned int value = num < 0 ? (unsigned int)(-num) : (unsigned int)num;
  char tmp[34];
  size_t pos = 0;
  do {
    tmp[pos++] = (char)('0' + value % (unsigned int)base);
    value /= (unsigned int)base;
  } while (value && pos < sizeof(tmp) - 1);
  if (num < 0) tmp[pos++] = '-';

  for (size_t i = 0; i < pos; i++) buffer[i] = tmp[pos - i - 1];
  buffer[pos] = '\0';
  return buffer;
}

int get_field_copy(const char *line, unsigned int field,
                   char *output, size_t output_size) {
  if (!line || !field || !output || output_size == 0) return -EINVAL;

  const char *cursor = line;
  unsigned int current = 0;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') cursor++;
    if (!*cursor) break;

    const char *start = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t' &&
           *cursor != '\r' && *cursor != '\n') cursor++;
    current++;
    if (current == field) {
      size_t len = (size_t)(cursor - start);
      if (len + 1 > output_size) return -ENOSPC;
      memcpy(output, start, len);
      output[len] = '\0';
      return 0;
    }
  }
  return -ENOENT;
}

int click_write(const char *path, const char *value) {
  if (!path || !value) return -EINVAL;
  FILE *file = fopen(path, "w");
  if (!file) return -errno;

  int rc = 0;
  if (fprintf(file, "%s", value) < 0 || fflush(file) != 0) rc = -EIO;
  if (fclose(file) != 0 && rc == 0) rc = -EIO;
  return rc;
}

int click_read(const char *path, char *buffer, size_t buffer_size) {
  if (!path || !buffer || buffer_size == 0) return -EINVAL;
  FILE *file = fopen(path, "r");
  if (!file) return -errno;

  if (!fgets(buffer, (int)buffer_size, file)) {
    int rc = ferror(file) ? -EIO : -ENODATA;
    fclose(file);
    return rc;
  }
  fclose(file);
  buffer[strcspn(buffer, "\r\n")] = '\0';
  return 0;
}

static int switch_port_path(const char *handler, char *path, size_t path_size) {
  if (!handler || !path || path_size == 0) return -EINVAL;
  int len = snprintf(path, path_size, "/click/switch_port_table/%s", handler);
  if (len < 0 || (size_t)len >= path_size) return -ENAMETOOLONG;
  return 0;
}

int write_switch_port_table(const char *handler, const char *value) {
  char path[256];
  int rc = switch_port_path(handler, path, sizeof(path));
  return rc == 0 ? click_write(path, value) : rc;
}

int read_switch_port_table(const char *handler, unsigned int port,
                           char *buffer, size_t buffer_size) {
  char path[256];
  int rc = switch_port_path(handler, path, sizeof(path));
  if (rc != 0) return rc;

  FILE *file = fopen(path, "r");
  if (!file) return -errno;

  char line[512];
  unsigned int row = 0;
  bool found = false;
  while (fgets(line, sizeof(line), file)) {
    if (row++ == port) {
      size_t len = strcspn(line, "\r\n");
      if (len + 1 > buffer_size) {
        fclose(file);
        return -ENOSPC;
      }
      memcpy(buffer, line, len);
      buffer[len] = '\0';
      found = true;
      break;
    }
  }
  fclose(file);
  return found ? 0 : -ENOENT;
}
