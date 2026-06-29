#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <linux/input.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SECURITY_DEFAULT "/config/postmerkos/security.json"
#define CONTROLS_DEFAULT "/run/postmerkos/hardware-controls.json"
#define STATUS_DEFAULT "/run/postmerkos/button-status.json"
#define LEDCTL_DEFAULT "/usr/sbin/postmerkos-ledctl"
#define FACTORY_RESET_DEFAULT "/bin/fw_factory_reset"
#define FWUPDATE_LOCK_DEFAULT "/run/fwupdate.lock"
#define POLL_INTERVAL_MS 50
#define DEBOUNCE_MS 150
#define ARM_RELEASE_MS 500
#define POLICY_RELOAD_MS 1000
#define BIT_WORD(n) ((n) / (8U * sizeof(unsigned long)))
#define BIT_MASK(n) (1UL << ((n) % (8U * sizeof(unsigned long))))

static volatile sig_atomic_t running = 1;

struct reset_policy {
  bool enabled;
  unsigned int hold_seconds;
};

struct reset_control {
  char backend[24];
  char path[256];
  char device[256];
  int code;
  int active;
  int gpio;
  uint64_t address;
  uint32_t mask;
  bool available;
  bool verified;
  bool destructive_enabled;
};

struct input_source {
  int fd;
  void *mapping;
  size_t mapping_length;
  volatile uint32_t *register_address;
};

static void stop_handler(int signal_number) {
  (void)signal_number;
  running = 0;
}

static long long monotonic_ms(void) {
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
  return (long long)value.tv_sec * 1000LL + value.tv_nsec / 1000000LL;
}

static void sleep_ms(unsigned int milliseconds) {
  struct timespec delay = {
    .tv_sec = (time_t)(milliseconds / 1000U),
    .tv_nsec = (long)(milliseconds % 1000U) * 1000000L
  };
  while (nanosleep(&delay, &delay) != 0 && errno == EINTR && running) {}
}

static struct json_object *member(struct json_object *object, const char *key) {
  struct json_object *value = NULL;
  return object && json_object_is_type(object, json_type_object) &&
      json_object_object_get_ex(object, key, &value) ? value : NULL;
}

static const char *string_member(struct json_object *object, const char *key,
                                 const char *fallback) {
  struct json_object *value = member(object, key);
  return value && json_object_is_type(value, json_type_string)
      ? json_object_get_string(value) : fallback;
}

static int int_member(struct json_object *object, const char *key, int fallback) {
  struct json_object *value = member(object, key);
  return value && json_object_is_type(value, json_type_int)
      ? json_object_get_int(value) : fallback;
}

static uint64_t uint64_member(struct json_object *object, const char *key,
                              uint64_t fallback) {
  struct json_object *value = member(object, key);
  return value && json_object_is_type(value, json_type_int)
      ? json_object_get_uint64(value) : fallback;
}

static bool bool_member(struct json_object *object, const char *key,
                        bool fallback) {
  struct json_object *value = member(object, key);
  return value && json_object_is_type(value, json_type_boolean)
      ? json_object_get_boolean(value) : fallback;
}

static void load_policy(struct reset_policy *policy) {
  policy->enabled = false;
  policy->hold_seconds = 10;
  const char *path = getenv("POSTMERKOS_SECURITY_FILE");
  if (!path || !*path) path = SECURITY_DEFAULT;
  struct json_object *root = json_object_from_file(path);
  if (!root) return;
  struct json_object *button = member(root, "reset_button");
  if (button && json_object_is_type(button, json_type_object)) {
    const char *action = string_member(button, "action", "");
    policy->enabled = bool_member(button, "enabled", false) &&
                      !strcmp(action, "factory-reset");
    int seconds = int_member(button, "hold_seconds", 10);
    if (seconds >= 3 && seconds <= 60)
      policy->hold_seconds = (unsigned int)seconds;
  }
  json_object_put(root);
}

static bool load_control(struct reset_control *control) {
  memset(control, 0, sizeof(*control));
  control->gpio = -1;
  snprintf(control->backend, sizeof(control->backend), "unidentified");
  const char *path = getenv("POSTMERKOS_HARDWARE_CONTROLS");
  if (!path || !*path) path = CONTROLS_DEFAULT;
  struct json_object *root = json_object_from_file(path);
  if (!root) return false;
  struct json_object *button = member(root, "reset_button");
  if (button) {
    snprintf(control->backend, sizeof(control->backend), "%s",
             string_member(button, "backend", "unidentified"));
    snprintf(control->path, sizeof(control->path), "%s",
             string_member(button, "path", ""));
    snprintf(control->device, sizeof(control->device), "%s",
             string_member(button, "device", ""));
    control->code = int_member(button, "code", KEY_RESTART);
    control->active = int_member(button, "active", 1);
    control->gpio = int_member(button, "gpio", -1);
    control->address = uint64_member(button, "address", 0);
    control->mask = (uint32_t)uint64_member(button, "mask", 0);
    control->available = bool_member(button, "available", false);
    control->verified = bool_member(button, "verified", false);
    control->destructive_enabled =
        bool_member(button, "destructive_enabled", false);
  }
  json_object_put(root);
  return control->available;
}

static void write_status(const char *state, const struct reset_policy *policy,
                         const struct reset_control *control, int progress,
                         const char *detail) {
  const char *path = getenv("POSTMERKOS_BUTTON_STATUS");
  if (!path || !*path) path = STATUS_DEFAULT;
  char temporary[512];
  snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid());
  struct json_object *root = json_object_new_object();
  if (!root) return;
  json_object_object_add(root, "state", json_object_new_string(state));
  json_object_object_add(root, "enabled", json_object_new_boolean(policy->enabled));
  json_object_object_add(root, "hold_seconds",
                         json_object_new_int((int)policy->hold_seconds));
  json_object_object_add(root, "backend",
                         json_object_new_string(control->backend));
  json_object_object_add(root, "gpio", json_object_new_int(control->gpio));
  json_object_object_add(root, "active_low",
                         json_object_new_boolean(control->active == 0));
  json_object_object_add(root, "address",
                         json_object_new_uint64(control->address));
  json_object_object_add(root, "mask",
                         json_object_new_uint64(control->mask));
  json_object_object_add(root, "available",
                         json_object_new_boolean(control->available));
  json_object_object_add(root, "verified",
                         json_object_new_boolean(control->verified));
  json_object_object_add(root, "destructive_enabled",
                         json_object_new_boolean(control->destructive_enabled));
  json_object_object_add(root, "progress", json_object_new_int(progress));
  if (detail)
    json_object_object_add(root, "detail", json_object_new_string(detail));
  if (json_object_to_file_ext(temporary, root, JSON_C_TO_STRING_PRETTY) == 0)
    (void)rename(temporary, path);
  else
    (void)unlink(temporary);
  json_object_put(root);
}

static const char *env_or_default(const char *name, const char *fallback) {
  const char *value = getenv(name);
  return value && *value ? value : fallback;
}

static int run_led(const char *action, int value) {
  const char *ledctl = env_or_default("POSTMERKOS_LEDCTL", LEDCTL_DEFAULT);
  if (access(ledctl, X_OK) != 0) return -ENOENT;
  pid_t child = fork();
  if (child < 0) return -errno;
  if (child == 0) {
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    if (!strcmp(action, "acquire") || !strcmp(action, "release"))
      execl(ledctl, ledctl, action, "reset", (char *)NULL);
    else if (value >= 0)
      execl(ledctl, ledctl, action, text, (char *)NULL);
    else
      execl(ledctl, ledctl, action, (char *)NULL);
    _exit(127);
  }
  int status = 0;
  while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -EIO;
}

static int evdev_pressed(int fd, int code, bool *pressed) {
  if (code < 0 || code > KEY_MAX) return -EINVAL;
  unsigned long bits[BIT_WORD(KEY_MAX) + 1];
  memset(bits, 0, sizeof(bits));
  if (ioctl(fd, EVIOCGKEY(sizeof(bits)), bits) < 0) return -errno;
  *pressed = (bits[BIT_WORD(code)] & BIT_MASK(code)) != 0;
  return 0;
}

static int level_pressed(int fd, int active, bool *pressed) {
  char buffer[32];
  if (lseek(fd, 0, SEEK_SET) < 0) return -errno;
  ssize_t count = read(fd, buffer, sizeof(buffer) - 1);
  if (count < 0) return -errno;
  if (count == 0) return -EIO;
  buffer[count] = '\0';
  char *end = NULL;
  long value = strtol(buffer, &end, 10);
  if (end == buffer) return -EIO;
  *pressed = value == active;
  return 0;
}

static int control_pressed(const struct reset_control *control,
                           const struct input_source *source, bool *pressed) {
  if (!strcmp(control->backend, "jaguar1-mmio")) {
    if (!source->register_address || control->mask == 0) return -ENODEV;
    __sync_synchronize();
    uint32_t value = *source->register_address;
    __sync_synchronize();
    int level = (value & control->mask) != 0 ? 1 : 0;
    *pressed = level == control->active;
    return 0;
  }
  if (!strcmp(control->backend, "evdev"))
    return evdev_pressed(source->fd, control->code, pressed);
  if (!strcmp(control->backend, "sysfs") ||
      !strcmp(control->backend, "sysfs-gpio") ||
      !strcmp(control->backend, "click"))
    return level_pressed(source->fd, control->active, pressed);
  return -ENODEV;
}

static int open_control(const struct reset_control *control,
                        struct input_source *source) {
  memset(source, 0, sizeof(*source));
  source->fd = -1;
  source->mapping = MAP_FAILED;
  if (!strcmp(control->backend, "jaguar1-mmio")) {
    if (!control->address || !control->mask) return -EINVAL;
    const char *device = control->device[0] ? control->device : "/dev/mem";
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return -EINVAL;
    uint64_t page_mask = (uint64_t)page_size - 1U;
    uint64_t page_address = control->address & ~page_mask;
    size_t register_offset = (size_t)(control->address - page_address);
    source->fd = open(device, O_RDONLY | O_SYNC | O_CLOEXEC);
    if (source->fd < 0) return -errno;
    source->mapping_length = (size_t)page_size;
    source->mapping = mmap(NULL, source->mapping_length, PROT_READ, MAP_SHARED,
                           source->fd, (off_t)page_address);
    if (source->mapping == MAP_FAILED) {
      int error = errno;
      close(source->fd);
      source->fd = -1;
      return -error;
    }
    source->register_address = (volatile uint32_t *)
        ((unsigned char *)source->mapping + register_offset);
    return 0;
  }

  const char *path = NULL;
  if (!strcmp(control->backend, "evdev")) path = control->device;
  else if (!strcmp(control->backend, "sysfs") ||
           !strcmp(control->backend, "sysfs-gpio") ||
           !strcmp(control->backend, "click")) path = control->path;
  if (!path || !*path) return -ENODEV;
  source->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  return source->fd >= 0 ? 0 : -errno;
}

static void close_control(struct input_source *source) {
  if (source->mapping != MAP_FAILED && source->mapping)
    (void)munmap(source->mapping, source->mapping_length);
  if (source->fd >= 0) close(source->fd);
  source->fd = -1;
  source->mapping = MAP_FAILED;
  source->register_address = NULL;
}

static bool update_in_progress(void) {
  const char *lock = env_or_default("POSTMERKOS_FWUPDATE_LOCK",
                                    FWUPDATE_LOCK_DEFAULT);
  return access(lock, F_OK) == 0;
}

static int invoke_factory_reset(void) {
  const char *program = env_or_default("POSTMERKOS_FACTORY_RESET",
                                       FACTORY_RESET_DEFAULT);
  fprintf(stderr,
          "reset button: verified hold completed; requesting factory reset\n");
  sync();
  execl(program, program, "--yes", (char *)NULL);
  int error = errno;
  fprintf(stderr, "reset button: unable to execute %s: %s\n", program,
          strerror(error));
  return -error;
}

static bool policy_equal(const struct reset_policy *a,
                         const struct reset_policy *b) {
  return a->enabled == b->enabled && a->hold_seconds == b->hold_seconds;
}

int main(void) {
  signal(SIGINT, stop_handler);
  signal(SIGTERM, stop_handler);
  (void)mkdir("/run/postmerkos", 0755);

  while (running) {
    struct reset_policy policy;
    struct reset_control control;
    load_policy(&policy);
    (void)load_control(&control);
    if (!policy.enabled) {
      write_status("disabled", &policy, &control, 0,
                   "physical reset-button policy is disabled");
      sleep_ms(POLICY_RELOAD_MS);
      continue;
    }
    if (!control.available || !control.verified ||
        !control.destructive_enabled ||
        !strcmp(control.backend, "unidentified")) {
      write_status("unavailable", &policy, &control, 0,
                   "no exact-model hardware-verified reset input is enabled");
      sleep_ms(POLICY_RELOAD_MS);
      continue;
    }

    struct input_source input;
    int input_result = open_control(&control, &input);
    if (input_result < 0) {
      control.available = false;
      write_status("unavailable", &policy, &control, 0,
                   strerror(-input_result));
      sleep_ms(POLICY_RELOAD_MS);
      continue;
    }

    bool armed = false;
    bool held = false;
    bool led_owned = false;
    bool inhibited_reported = false;
    bool raw_pressed = false;
    bool debounced_pressed = false;
    bool have_sample = false;
    long long raw_since = 0;
    long long release_since = 0;
    long long held_since = 0;
    long long last_policy_check = monotonic_ms();
    int last_progress = -1;
    write_status("waiting-release", &policy, &control, 0,
                 "release the button once after boot to arm factory reset");

    while (running) {
      bool sample = false;
      int rc = control_pressed(&control, &input, &sample);
      if (rc != 0) {
        write_status("unavailable", &policy, &control, 0, strerror(-rc));
        break;
      }
      long long now = monotonic_ms();
      if (!have_sample || sample != raw_pressed) {
        raw_pressed = sample;
        raw_since = now;
        have_sample = true;
      }
      if (now - raw_since >= DEBOUNCE_MS)
        debounced_pressed = raw_pressed;

      if (now - last_policy_check >= POLICY_RELOAD_MS) {
        struct reset_policy updated;
        load_policy(&updated);
        if (!policy_equal(&policy, &updated)) break;
        last_policy_check = now;
      }

      if (!armed) {
        if (debounced_pressed) {
          release_since = 0;
        } else {
          if (release_since == 0) release_since = now;
          if (now - release_since >= ARM_RELEASE_MS) {
            armed = true;
            write_status("ready", &policy, &control, 0,
                         "hold continuously to erase persistent settings");
          }
        }
        sleep_ms(POLL_INTERVAL_MS);
        continue;
      }

      if (update_in_progress()) {
        if (held) {
          held = false;
          held_since = 0;
          last_progress = -1;
          if (led_owned) (void)run_led("release", -1);
          led_owned = false;
        }
        if (!inhibited_reported) {
          write_status("inhibited", &policy, &control, 0,
                       "firmware operation in progress; physical reset is blocked");
          inhibited_reported = true;
        }
        sleep_ms(POLL_INTERVAL_MS);
        continue;
      }
      if (inhibited_reported) {
        write_status("ready", &policy, &control, 0,
                     "firmware interlock cleared; hold continuously to reset");
        inhibited_reported = false;
      }

      if (debounced_pressed && !held) {
        held = true;
        held_since = now;
        last_progress = -1;
        led_owned = run_led("acquire", -1) == 0;
        fprintf(stderr,
                "reset button: countdown started; release to cancel\n");
      }
      if (debounced_pressed && held) {
        long long elapsed = now - held_since;
        int progress = (int)((elapsed * 100LL) /
                             ((long long)policy.hold_seconds * 1000LL));
        if (progress > 100) progress = 100;
        int display_progress = progress >= 100 ? 100 : (progress / 5) * 5;
        if (display_progress != last_progress) {
          if (led_owned) (void)run_led("reset-progress", display_progress);
          write_status("countdown", &policy, &control, display_progress,
                       "release the button to cancel factory reset");
          last_progress = display_progress;
        }
        if (elapsed >= (long long)policy.hold_seconds * 1000LL) {
          if (update_in_progress()) {
            held = false;
            if (led_owned) (void)run_led("release", -1);
            led_owned = false;
            write_status("inhibited", &policy, &control, 0,
                         "firmware operation started; reset cancelled");
            continue;
          }
          if (led_owned) (void)run_led("reset-progress", 100);
          write_status("triggered", &policy, &control, 100,
                       "factory reset requested by verified physical input");
          close_control(&input);
          int reset_result = invoke_factory_reset();
          write_status("error", &policy, &control, 100,
                       strerror(-reset_result));
          (void)run_led("error", -1);
          sleep_ms(3000);
          (void)run_led("release", -1);
          return 1;
        }
      } else if (!debounced_pressed && held) {
        held = false;
        held_since = 0;
        last_progress = -1;
        if (led_owned) (void)run_led("release", -1);
        led_owned = false;
        write_status("ready", &policy, &control, 0,
                     "factory-reset countdown cancelled");
        fprintf(stderr, "reset button: released; reset cancelled\n");
      }
      sleep_ms(POLL_INTERVAL_MS);
    }
    if (led_owned) (void)run_led("release", -1);
    close_control(&input);
    sleep_ms(250);
  }
  return 0;
}
