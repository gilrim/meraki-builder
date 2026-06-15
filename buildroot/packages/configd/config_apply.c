#include "config_apply.h"
#include "click_global.h"
#include "click_port.h"
#include "config_file.h"
#include "configd.h"
#include "json_util.h"
#include "network.h"
#include "validation.h"
#include <libpostmerkos.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct json_object *config_create_defaults(struct apply_result *result) {
  struct json_object *config = click_read_ports(result);
  struct json_object *globals = click_read_globals(result);
  json_deep_merge(config, globals);
  json_object_put(globals);
  json_object_object_add(config, "network", network_default_config());
  return config;
}

static struct json_object *load_valid_path(const char *path,
                                           char *error, size_t error_size) {
  struct json_object *config = load_json_file(path);
  if (!config) {
    if (error && error_size) snprintf(error, error_size, "unable to parse %s", path);
    return NULL;
  }
  if (validate_configuration(config, error, error_size) != 0) {
    json_object_put(config);
    return NULL;
  }
  return config;
}

struct json_object *config_load_or_create(char *error, size_t error_size) {
  struct json_object *config = load_valid_path(config_file, error, error_size);
  if (config) return config;

  char backup[512];
  int copied = snprintf(backup, sizeof(backup), "%s.bak", config_file);
  if (copied > 0 && (size_t)copied < sizeof(backup)) {
    char backup_error[256];
    struct json_object *restored = load_valid_path(backup, backup_error,
                                                   sizeof(backup_error));
    if (restored) {
      char save_error[256];
      /* Do not let save_config_file() replace the known-good backup with the
       * invalid primary file that caused this recovery path. */
      unlink(config_file);
      if (save_config_file(restored, save_error, sizeof(save_error)) != 0)
        fprintf(stderr, "%s config: failed to restore backup: %s\n",
                get_time(), save_error);
      return restored;
    }
  }

  struct apply_result defaults_result;
  apply_result_init(&defaults_result);
  config = config_create_defaults(&defaults_result);
  if (validate_configuration(config, error, error_size) != 0) {
    apply_result_cleanup(&defaults_result);
    json_object_put(config);
    return NULL;
  }
  if (!dry_run && save_config_file(config, error, error_size) != 0) {
    apply_result_cleanup(&defaults_result);
    json_object_put(config);
    return NULL;
  }
  apply_result_cleanup(&defaults_result);
  return config;
}

int config_apply_full(struct json_object *config, struct apply_result *result) {
  click_apply_globals_full(config, result);
  click_apply_ports_full(config, result);
  return network_manager_configure(config, result, false);
}

int config_apply_delta(struct json_object *full_config,
                       struct json_object *delta,
                       struct apply_result *result,
                       bool defer_network) {
  click_apply_globals_delta(full_config, delta, result);
  click_apply_ports_delta(full_config, delta, result);
  struct json_object *network = NULL;
  if (json_object_object_get_ex(delta, "network", &network))
    return network_manager_configure(full_config, result, defer_network);
  return 0;
}

int config_merge_validate_save_apply(struct json_object *delta,
                                     struct json_object **saved_config,
                                     struct apply_result *result,
                                     bool defer_network,
                                     char *error, size_t error_size) {
  if (!delta || !json_object_is_type(delta, json_type_object)) {
    snprintf(error, error_size, "configuration delta must be an object");
    return -EINVAL;
  }
  struct json_object *current = load_config_file();
  if (!current) {
    snprintf(error, error_size, "current configuration is unavailable");
    return -ENOENT;
  }
  struct json_object *merged = json_deep_copy_object(current);
  json_object_put(current);
  if (!merged) {
    snprintf(error, error_size, "unable to copy current configuration");
    return -ENOMEM;
  }
  json_deep_merge(merged, delta);
  if (validate_configuration(merged, error, error_size) != 0) {
    json_object_put(merged);
    return -EINVAL;
  }
  int rc = save_config_file(merged, error, error_size);
  if (rc != 0) {
    json_object_put(merged);
    return rc;
  }
  config_apply_delta(merged, delta, result, defer_network);
  config_file_set_runtime_error(NULL);
  if (saved_config) *saved_config = merged;
  else json_object_put(merged);
  return 0;
}
