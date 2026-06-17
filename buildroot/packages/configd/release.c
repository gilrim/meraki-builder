#include "release.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char cached_version[64];

struct json_object *release_info_load(void) {
  const char *path = getenv("POSTMERKOS_RELEASE_FILE");
  if (!path || !*path) path = "/etc/postmerkos-release.json";
  struct json_object *release = json_object_from_file(path);
  if (!release || !json_object_is_type(release, json_type_object)) {
    if (release) json_object_put(release);
    release = json_object_new_object();
    json_object_object_add(release, "version", json_object_new_string("unknown"));
    json_object_object_add(release, "metadata_present", json_object_new_boolean(0));
    return release;
  }
  json_object_object_add(release, "metadata_present", json_object_new_boolean(1));
  return release;
}

const char *release_version(void) {
  struct json_object *release = release_info_load();
  struct json_object *value = NULL;
  const char *version = "unknown";
  if (json_object_object_get_ex(release, "version", &value) &&
      json_object_is_type(value, json_type_string))
    version = json_object_get_string(value);
  snprintf(cached_version, sizeof(cached_version), "%s", version);
  json_object_put(release);
  return cached_version;
}
