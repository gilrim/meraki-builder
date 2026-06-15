#include <libpostmerkos.h>
#include <libpd690xx.h>
#include "pd690xx_meraki.h"
#include "clickswstatus.h"

#include <dirent.h>
#include <json-c/json.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static bool poe_capable;
static struct pd690xx_cfg pd690xx = {
    {-1, -1},
    {PD690XX0_I2C_ADDR, PD690XX1_I2C_ADDR,
     PD690XX2_I2C_ADDR, PD690XX3_I2C_ADDR},
    {0, 0, 0, 0}
};

static void add_cpu_temperatures(struct json_object *temperatures) {
  struct json_object *cpu = json_object_new_array();
  json_object_object_add(temperatures, "cpu", cpu);

  const char *directory = "/sys/class/thermal";
  DIR *dir = opendir(directory);
  if (!dir) return;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (!starts_with(entry->d_name, "thermal_")) continue;

    char path[256];
    int length = snprintf(path, sizeof(path), "%s/%s/temp",
                          directory, entry->d_name);
    if (length < 0 || (size_t)length >= sizeof(path)) continue;

    FILE *file = fopen(path, "r");
    if (!file) continue;

    long millidegrees = 0;
    if (fscanf(file, "%ld", &millidegrees) == 1) {
      json_object_array_add(cpu,
                            json_object_new_double(millidegrees / 1000.0));
    }
    fclose(file);
  }
  closedir(dir);
}

static void add_poe_temperatures(struct json_object *temperatures) {
  if (!poe_capable) return;

  struct json_object *poe = json_object_new_array();
  json_object_object_add(temperatures, "poe", poe);

  int controller_count = pd690xx_pres_count(&pd690xx);
  float *values = get_temp(&pd690xx);
  if (!values) return;

  for (int i = 0; i < controller_count; i++) {
    json_object_array_add(poe, json_object_new_double(values[i]));
  }
  free(values);
}

static void add_ports(struct json_object *root) {
  struct json_object *ports = json_object_new_object();
  json_object_object_add(root, "ports", ports);

  FILE *file = fopen(PORTS_FILE, "r");
  if (!file) return;

  char line[256];
  unsigned int port = 0;
  while (fgets(line, sizeof(line), file)) {
    if (port++ == 0) continue; /* header */

    char port_name[16];
    snprintf(port_name, sizeof(port_name), "%u", port - 1);
    struct json_object *entry = json_object_new_object();
    json_object_object_add(ports, port_name, entry);

    struct json_object *link = json_object_new_object();
    json_object_object_add(entry, "link", link);

    char field[32];
    int established = 0;
    int speed = 0;
    if (get_field_copy(line, 2, field, sizeof(field)) == 0)
      established = atoi(field);
    if (get_field_copy(line, 3, field, sizeof(field)) == 0)
      speed = atoi(field);
    json_object_object_add(link, "established",
                           json_object_new_boolean(established));
    json_object_object_add(link, "speed", json_object_new_int(speed));

    int physical_port = (int)port - 1;
    if (poe_capable && physical_port > 0 &&
        physical_port <= 12 * pd690xx_pres_count(&pd690xx)) {
      struct json_object *poe = json_object_new_object();
      json_object_object_add(entry, "poe", poe);
      double power = port_power(&pd690xx, physical_port);
      if (power >= 0.0)
        json_object_object_add(poe, "power", json_object_new_double(power));
    }
  }
  fclose(file);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  i2c_init(&pd690xx);
  poe_capable = pd690xx_pres_count(&pd690xx) > 0;

  struct json_object *root = json_object_new_object();
  json_object_object_add(root, "datetime", json_object_new_string(get_time()));

  struct json_object *temperatures = json_object_new_object();
  json_object_object_add(root, "temperature", temperatures);
  add_cpu_temperatures(temperatures);
  add_poe_temperatures(temperatures);
  add_ports(root);

  puts(json_object_to_json_string_ext(
      root, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY));
  json_object_put(root);
  i2c_close(&pd690xx);
  return 0;
}
