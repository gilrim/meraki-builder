#define _GNU_SOURCE
#include "system_identity.h"
#include <assert.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int service_reconfigure(const char *service, char *error, size_t error_size) {
  (void)error; (void)error_size;
  assert(strcmp(service, "mdns") == 0);
  return 0;
}

int main(void) {
  char temp[] = "/tmp/configd-identity-XXXXXX";
  int fd = mkstemp(temp); assert(fd >= 0); close(fd); unlink(temp);
  setenv("CONFIGD_SYSTEM_POLICY", temp, 1);
  setenv("CONFIGD_SYSTEM_POLICY_DEFAULT", temp, 1);
  setenv("CONFIGD_HOSTNAME_DRY_RUN", "1", 1);
  char error[256] = {0};
  assert(system_identity_validate_hostname("postmerkos", error, sizeof(error)) == 0);
  assert(system_identity_validate_hostname("bad.name", error, sizeof(error)) != 0);
  assert(system_identity_validate_hostname("-bad", error, sizeof(error)) != 0);
  assert(system_identity_validate_hostname("localhost", error, sizeof(error)) != 0);
  struct json_object *policy = json_object_new_object();
  json_object_object_add(policy, "hostname", json_object_new_string("Core-Switch"));
  assert(system_identity_save(policy, error, sizeof(error)) == 0);
  json_object_put(policy);
  struct json_object *saved = json_object_from_file(temp); assert(saved);
  struct json_object *hostname = NULL; assert(json_object_object_get_ex(saved, "hostname", &hostname));
  assert(strcmp(json_object_get_string(hostname), "core-switch") == 0);
  json_object_put(saved);
  struct json_object *status = system_identity_status_json(); assert(status);
  struct json_object *configured = NULL; assert(json_object_object_get_ex(status, "configured_hostname", &configured));
  assert(strcmp(json_object_get_string(configured), "core-switch") == 0);
  struct json_object *registration = NULL; assert(json_object_object_get_ex(status, "dhcp_hostname_registration_supported", &registration));
  assert(!json_object_get_boolean(registration));
  struct json_object *verified = NULL; assert(json_object_object_get_ex(status, "advertised_name_verified", &verified));
  assert(!json_object_get_boolean(verified));
  json_object_put(status); unlink(temp);
  puts("system identity tests passed"); return 0;
}
