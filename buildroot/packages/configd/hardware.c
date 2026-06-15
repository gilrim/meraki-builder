#include "hardware.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *env_or_default(const char *name, const char *fallback) {
  const char *value = getenv(name);
  return value && *value ? value : fallback;
}

static void trim(char *value) {
  if (!value) return;
  value[strcspn(value, "\r\n")] = '\0';
  while (*value == ' ' || *value == '\t') memmove(value, value + 1, strlen(value));
  size_t len = strlen(value);
  while (len && (value[len - 1] == ' ' || value[len - 1] == '\t'))
    value[--len] = '\0';
}

static int read_model(char *model, size_t size) {
  const char *override = getenv("CONFIGD_MODEL");
  if (override && *override) {
    snprintf(model, size, "%s", override);
    return 0;
  }

  const char *path = env_or_default("CONFIGD_BOARDINFO", "/etc/boardinfo");
  FILE *file = fopen(path, "r");
  if (!file) return -errno;

  char line[128];
  while (fgets(line, sizeof(line), file)) {
    trim(line);
    const char *value = line;
    if (strncmp(line, "MODEL=", 6) == 0) value = line + 6;
    if (strncmp(value, "MS", 2) == 0) {
      snprintf(model, size, "%s", value);
      fclose(file);
      return 0;
    }
  }
  fclose(file);
  return -ENOENT;
}

static unsigned int model_port_count(const char *model) {
  if (!model) return 0;
  if (!strcmp(model, "MS220-8") || !strcmp(model, "MS220-8P")) return 10;
  if (!strcmp(model, "MS220-24") || !strcmp(model, "MS220-24P") ||
      !strcmp(model, "MS22") || !strcmp(model, "MS22P")) return 24;
  if (!strcmp(model, "MS320-24") || !strcmp(model, "MS320-24P")) return 28;
  if (!strcmp(model, "MS220-48") || !strcmp(model, "MS220-48P") ||
      !strcmp(model, "MS320-48") || !strcmp(model, "MS320-48P") ||
      !strcmp(model, "MS42") || !strcmp(model, "MS42P")) return 52;
  return 0;
}

static unsigned int model_poe_ports(const char *model) {
  if (!model) return 0;
  if (!strcmp(model, "MS220-8P")) return 8;
  if (!strcmp(model, "MS220-24P") || !strcmp(model, "MS320-24P") ||
      !strcmp(model, "MS22P")) return 24;
  if (!strcmp(model, "MS220-48P") || !strcmp(model, "MS320-48P") ||
      !strcmp(model, "MS42P")) return 48;
  return 0;
}

static unsigned int read_port_count(void) {
  const char *override = getenv("CONFIGD_PORT_COUNT");
  if (override && *override) return (unsigned int)strtoul(override, NULL, 10);

  const char *path = env_or_default("CONFIGD_NUM_PORTS", "/tmp/NUM_PORTS");
  FILE *file = fopen(path, "r");
  unsigned int count = 0;
  if (file) {
    if (fscanf(file, "%u", &count) != 1) count = 0;
    fclose(file);
  }
  return count;
}

int hardware_init(struct hardware_info *info, struct pd690xx_cfg *pd690xx) {
  if (!info || !pd690xx) return -EINVAL;
  memset(info, 0, sizeof(*info));
  if (read_model(info->model, sizeof(info->model)) != 0)
    snprintf(info->model, sizeof(info->model), "unknown");

  info->port_count = read_port_count();
  if (!info->port_count) info->port_count = model_port_count(info->model);
  info->poe_port_count = model_poe_ports(info->model);
  info->poe_supported = info->poe_port_count > 0;

  const char *skip_i2c = getenv("CONFIGD_SKIP_I2C");
  if (info->poe_supported && (!skip_i2c || strcmp(skip_i2c, "1") != 0))
    i2c_init(pd690xx);
  info->poe_controller_count = (unsigned int)pd690xx_pres_count(pd690xx);
  info->poe_available = info->poe_supported && info->poe_controller_count > 0;
  return 0;
}

bool hardware_port_valid(const struct hardware_info *info, unsigned int port) {
  return info && port > 0 && (!info->port_count || port <= info->port_count);
}

bool hardware_port_supports_poe(const struct hardware_info *info,
                                unsigned int port) {
  return info && info->poe_supported && port > 0 &&
         port <= info->poe_port_count;
}

struct json_object *hardware_capabilities_json(const struct hardware_info *info) {
  struct json_object *caps = json_object_new_object();
  struct json_object *poe = json_object_new_object();
  json_object_object_add(caps, "port_count",
      json_object_new_int(info ? (int)info->port_count : 0));
  json_object_object_add(poe, "supported",
      json_object_new_boolean(info && info->poe_supported));
  json_object_object_add(poe, "available",
      json_object_new_boolean(info && info->poe_available));
  json_object_object_add(poe, "ports",
      json_object_new_int(info ? (int)info->poe_port_count : 0));
  json_object_object_add(poe, "controllers",
      json_object_new_int(info ? (int)info->poe_controller_count : 0));
  json_object_object_add(poe, "modes", json_object_new_array());
  struct json_object *modes;
  json_object_object_get_ex(poe, "modes", &modes);
  json_object_array_add(modes, json_object_new_string("af"));
  json_object_array_add(modes, json_object_new_string("at"));
  json_object_object_add(caps, "poe", poe);
  return caps;
}
