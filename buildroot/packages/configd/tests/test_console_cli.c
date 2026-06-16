#include "../configd.h"
#include "../console_cli.h"
#include "../network.h"
#include "../status.h"

#include <assert.h>
#include <json-c/json.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool dry_run = true;
const char *config_file = "/tmp/configd-console-test.json";
char meraki_mac[18] = "00:11:22:33:44:55";
struct hardware_info hardware = {
  .model = "MS42P",
  .port_count = 52,
  .poe_port_count = 48,
  .poe_controller_count = 4,
  .poe_supported = true,
  .poe_available = true,
};
struct pd690xx_cfg pd690xx;

bool hardware_port_valid(const struct hardware_info *info, unsigned int port) {
  return info && port > 0 && port <= info->port_count;
}

int network_manager_observe(struct json_object *config) {
  (void)config;
  return 0;
}

struct json_object *get_status(void) {
  return json_tokener_parse(
      "{\"network\":{\"ipv4\":{\"configured_mode\":\"dhcp\","
      "\"source\":\"dhcp\",\"state\":\"bound\","
      "\"address\":\"192.0.2.20/24\",\"gateway\":\"192.0.2.1\","
      "\"mtu\":1500}},\"temperature\":{\"cpu\":[35.0]},"
      "\"ports\":{\"1\":{\"link\":{\"established\":true,"
      "\"speed\":1000},\"poe\":{\"power\":4.5}}},\"errors\":[]}");
}

static struct json_object *sample_config(void) {
  return json_tokener_parse(
      "{\"network\":{\"ipv4\":{\"mode\":\"dhcp\","
      "\"fallback_address\":\"169.254.0.10/16\",\"mtu\":1500}},"
      "\"stp\":{\"priority\":32768,\"hello_time\":2,"
      "\"forward_delay\":15,\"max_age\":20,\"hold_count\":6},"
      "\"lacp\":{\"enabled\":false},"
      "\"multicast\":{\"igmp_snooping\":true,"
      "\"igmp_querier_interval\":125,\"mld_snooping\":true,"
      "\"mld_querier_interval\":125},"
      "\"ports\":{\"1\":{\"enabled\":true,\"name\":\"router\","
      "\"speed\":\"auto\",\"flow_control\":false,\"eee\":true,"
      "\"storm_control\":true,\"vlan\":{\"mode\":\"trunk\","
      "\"pvid\":1,\"allowed\":\"1-4094\",\"untagged_vid\":1,"
      "\"ingress_filter\":true},\"stp\":{\"enabled\":true,"
      "\"priority\":128,\"cost\":0,\"edge\":false,"
      "\"auto_edge\":true},\"poe\":{\"enabled\":true,"
      "\"mode\":\"at\"}}}}}");
}

int main(void) {
  struct json_object *config = sample_config();
  assert(config);
  struct json_object *value = console_path_get(config, "ports.1.name");
  assert(value && strcmp(json_object_get_string(value), "router") == 0);
  assert(console_path_get(config, "ports.2.name") == NULL);

  char error[128] = "";
  struct json_object *delta = console_delta_from_path(
      "ports.1.poe.enabled", "false", error, sizeof(error));
  assert(delta);
  value = console_path_get(delta, "ports.1.poe.enabled");
  assert(value && json_object_is_type(value, json_type_boolean));
  assert(!json_object_get_boolean(value));
  json_object_put(delta);

  delta = console_delta_from_path("ports.1.name", "lab uplink",
                                  error, sizeof(error));
  assert(delta);
  value = console_path_get(delta, "ports.1.name");
  assert(value && strcmp(json_object_get_string(value), "lab uplink") == 0);
  json_object_put(delta);

  delta = console_delta_from_string_path("ports.1.name", "123",
                                         error, sizeof(error));
  assert(delta);
  value = console_path_get(delta, "ports.1.name");
  assert(value && json_object_is_type(value, json_type_string));
  assert(strcmp(json_object_get_string(value), "123") == 0);
  json_object_put(delta);

  assert(console_print_summary(config) == 0);
  assert(console_print_ports(config, 1, 1) == 0);
  assert(console_print_port(config, 1) == 0);

  const char *backup = "/tmp/configd-console-export.json";
  unlink(backup);
  assert(console_export_config(config, backup, error, sizeof(error)) == 0);
  struct json_object *restored = json_object_from_file(backup);
  assert(restored);
  value = console_path_get(restored, "ports.1.name");
  assert(value && strcmp(json_object_get_string(value), "router") == 0);
  json_object_put(restored);
  unlink(backup);
  json_object_put(config);
  puts("console CLI tests passed");
  return 0;
}
