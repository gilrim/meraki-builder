#include "system_ops.h"

#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    failures++;
  }
}

int main(void) {
  struct json_object *result = terminal_execute("printf test-output");
  struct json_object *output = NULL;
  struct json_object *exit_code = NULL;
  check(json_object_object_get_ex(result, "output", &output),
        "terminal result has output");
  check(output && !strcmp(json_object_get_string(output), "test-output"),
        "terminal captures stdout");
  check(json_object_object_get_ex(result, "exit_code", &exit_code) &&
        json_object_get_int(exit_code) == 0, "terminal returns exit status");
  json_object_put(result);

  char path[] = "/tmp/configd-fw-status-XXXXXX";
  int fd = mkstemp(path);
  check(fd >= 0, "status temp file created");
  if (fd >= 0) {
    const char payload[] = "{\"state\":\"writing\",\"progress\":42}";
    check(write(fd, payload, sizeof(payload) - 1) == (ssize_t)(sizeof(payload) - 1),
          "status payload written");
    close(fd);
    setenv("FWUPDATE_STATUS_FILE", path, 1);
    struct json_object *status = firmware_status_json();
    struct json_object *state = NULL;
    struct json_object *progress = NULL;
    check(json_object_object_get_ex(status, "state", &state) &&
          !strcmp(json_object_get_string(state), "writing"),
          "firmware status state parsed");
    check(json_object_object_get_ex(status, "progress", &progress) &&
          json_object_get_int(progress) == 42,
          "firmware status progress parsed");
    json_object_put(status);
    unlink(path);
  }

  if (failures) return 1;
  puts("system operation tests passed");
  return 0;
}
