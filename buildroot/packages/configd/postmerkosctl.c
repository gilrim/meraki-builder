#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define RESPONSE_MAX 262144

static const char *socket_path(void) {
  const char *path = getenv("POSTMERKOS_CONFIGD_SOCKET");
  return path && *path ? path : "/run/postmerkos/configd.sock";
}

static int connect_socket(void) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -errno;
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path()) >=
      (int)sizeof(address.sun_path)) { close(fd); return -ENAMETOOLONG; }
  if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
    int rc = -errno; close(fd); return rc;
  }
  return fd;
}

static struct json_object *request(const char *type, struct json_object *data) {
  int fd = connect_socket();
  if (fd < 0) {
    fprintf(stderr, "postmerkosctl: cannot connect to configd: %s\n", strerror(-fd));
    return NULL;
  }
  struct json_object *message = json_object_new_object();
  json_object_object_add(message, "type", json_object_new_string(type));
  if (data) json_object_object_add(message, "data", json_object_get(data));
  const char *text = json_object_to_json_string_ext(message, JSON_C_TO_STRING_PLAIN);
  size_t left = strlen(text);
  const char *cursor = text;
  while (left) {
    ssize_t wrote = write(fd, cursor, left);
    if (wrote < 0) { if (errno == EINTR) continue; break; }
    cursor += wrote; left -= (size_t)wrote;
  }
  write(fd, "\n", 1);
  shutdown(fd, SHUT_WR);
  char *buffer = malloc(RESPONSE_MAX + 1);
  if (!buffer) { close(fd); json_object_put(message); return NULL; }
  size_t used = 0;
  while (used < RESPONSE_MAX) {
    ssize_t got = read(fd, buffer + used, RESPONSE_MAX - used);
    if (got == 0) break;
    if (got < 0) { if (errno == EINTR) continue; break; }
    used += (size_t)got;
  }
  buffer[used] = '\0';
  close(fd);
  json_object_put(message);
  struct json_object *reply = json_tokener_parse(buffer);
  free(buffer);
  if (!reply) fprintf(stderr, "postmerkosctl: invalid response from configd\n");
  return reply;
}

static struct json_object *reply_data(struct json_object *reply) {
  struct json_object *type = NULL, *data = NULL;
  if (!reply || !json_object_object_get_ex(reply, "type", &type)) return NULL;
  if (!strcmp(json_object_get_string(type), "error")) {
    if (json_object_object_get_ex(reply, "data", &data)) {
      struct json_object *message = NULL;
      if (json_object_object_get_ex(data, "message", &message))
        fprintf(stderr, "postmerkosctl: %s\n", json_object_get_string(message));
    }
    return NULL;
  }
  return json_object_object_get_ex(reply, "data", &data) ? data : NULL;
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

static bool bool_member(struct json_object *object, const char *key, bool fallback) {
  struct json_object *value = member(object, key);
  return value && json_object_is_type(value, json_type_boolean)
      ? json_object_get_boolean(value) : fallback;
}

static bool capability_present(struct json_object *identity, const char *name) {
  struct json_object *caps = member(identity, "capabilities");
  if (!caps || !json_object_is_type(caps, json_type_array)) return false;
  for (size_t i = 0; i < json_object_array_length(caps); i++) {
    struct json_object *cap = json_object_array_get_idx(caps, i);
    if (cap && !strcmp(json_object_get_string(cap), name)) return true;
  }
  return false;
}

static void print_session_shell(struct json_object *identity) {
  const char *username = string_member(identity, "username", "");
  const char *role = string_member(identity, "role", "none");
  struct json_object *caps = member(identity, "capabilities");
  printf("POSTMERKOS_USERNAME='%s'\n", username);
  printf("POSTMERKOS_ROLE='%s'\n", role);
  fputs("POSTMERKOS_CAPABILITIES='", stdout);
  for (size_t i = 0; caps && json_object_is_type(caps, json_type_array) &&
       i < json_object_array_length(caps); i++) {
    struct json_object *cap = json_object_array_get_idx(caps, i);
    if (cap && json_object_is_type(cap, json_type_string))
      printf("%s%s", i ? " " : "", json_object_get_string(cap));
  }
  puts("'");
}

static void print_service_summary(struct json_object *data) {
  puts("SERVICE       STATE       ENABLED     AUTOSTART");
  puts("------------- ----------- ----------- -----------");
  const char *names[] = {"ssh", "web", "chrony"};
  const char *labels[] = {"SSH", "Web UI", "Chrony"};
  for (size_t i = 0; i < 3; i++) {
    struct json_object *service = member(data, names[i]);
    printf("%-13s %-11s %-11s %-11s\n", labels[i],
           bool_member(service, "running", false) ? "running" : "stopped",
           bool_member(service, "enabled", false) ? "yes" : "no",
           bool_member(service, "autostart", false) ? "yes" : "no");
  }
}

static struct json_object *session_data(void) {
  struct json_object *reply = request("session", NULL);
  struct json_object *data = reply_data(reply);
  struct json_object *copy = data ? json_object_get(data) : NULL;
  if (reply) json_object_put(reply);
  return copy;
}

static struct json_object *snapshot_data(void) {
  struct json_object *reply = request("snapshot.get", NULL);
  struct json_object *data = reply_data(reply);
  struct json_object *copy = data ? json_object_get(data) : NULL;
  if (reply) json_object_put(reply);
  return copy;
}

static struct json_object *port_object(struct json_object *root, unsigned int port) {
  struct json_object *ports = member(root, "ports");
  char key[16]; snprintf(key, sizeof(key), "%u", port);
  return member(ports, key);
}

static void print_summary(struct json_object *snapshot) {
  struct json_object *status = member(snapshot, "status");
  struct json_object *caps = member(status, "capabilities");
  struct json_object *release = member(status, "release");
  struct json_object *network = member(member(status, "network"), "ipv4");
  printf("Model:              %s\n", string_member(status, "device", "unknown"));
  printf("Firmware:           %s\n", string_member(release, "version", "unknown"));
  printf("Compatibility:      %s\n", string_member(caps, "compatibility", "untested"));
  printf("Ports:              %d total, %d copper, %d uplink\n",
      int_member(caps, "port_count", 0), int_member(caps, "copper_ports", 0),
      int_member(caps, "uplink_ports", 0));
  printf("Management mode:    %s\n", string_member(network, "configured_mode", "unknown"));
  printf("Management state:   %s\n", string_member(network, "state", "unknown"));
  printf("Management address: %s\n", string_member(network, "address", "unavailable"));
  printf("Date/time:          %s\n", string_member(status, "datetime", "unknown"));
  if (bool_member(member(status, "security"), "default_password_active", false))
    puts("WARNING: The root password is still set to the device serial number.");
}

static void print_ports(struct json_object *snapshot, unsigned int first,
                        unsigned int last) {
  struct json_object *status = member(snapshot, "status");
  struct json_object *config = member(snapshot, "config");
  puts("PORT  LINK   SPEED  ADMIN     POE          VLAN  MODE    NAME");
  puts("----  -----  -----  --------  -----------  ----  ------  ------------------------");
  for (unsigned int port = first; port <= last; port++) {
    struct json_object *runtime = port_object(status, port);
    struct json_object *desired = port_object(config, port);
    struct json_object *link = member(runtime, "link");
    struct json_object *vlan = member(desired, "vlan");
    struct json_object *poe = member(desired, "poe");
    char poe_text[24] = "n/a";
    if (poe) snprintf(poe_text, sizeof(poe_text), "%s",
        bool_member(poe, "enabled", false) ? string_member(poe, "mode", "on") : "off");
    printf("%-4u  %-5s  %-5d  %-8s  %-11s  %-4d  %-6s  %s\n", port,
        bool_member(link, "established", false) ? "up" : "down",
        int_member(link, "speed", 0), bool_member(desired, "enabled", true) ? "enabled" : "disabled",
        poe_text, int_member(vlan, "pvid", 1), string_member(vlan, "mode", "access"),
        string_member(desired, "name", ""));
  }
}

static void print_port(struct json_object *snapshot, unsigned int port) {
  struct json_object *status = member(snapshot, "status");
  struct json_object *config = member(snapshot, "config");
  struct json_object *runtime = port_object(status, port);
  struct json_object *desired = port_object(config, port);
  if (!desired) { fprintf(stderr, "postmerkosctl: unknown port %u\n", port); return; }
  struct json_object *link = member(runtime, "link");
  struct json_object *vlan = member(desired, "vlan");
  struct json_object *stp = member(desired, "stp");
  struct json_object *poe = member(desired, "poe");
  printf("Port %u\n", port);
  printf("  Name:              %s\n", string_member(desired, "name", ""));
  printf("  Administrative:    %s\n", bool_member(desired, "enabled", true) ? "enabled" : "disabled");
  printf("  Link:              %s at %d Mbps\n", bool_member(link, "established", false) ? "up" : "down", int_member(link, "speed", 0));
  printf("  Configured speed:  %s\n", string_member(desired, "speed", "auto"));
  printf("  VLAN:              %s, PVID %d, allowed %s\n", string_member(vlan, "mode", "access"), int_member(vlan, "pvid", 1), string_member(vlan, "allowed", ""));
  printf("  STP:               %s, priority %d, cost %d\n", bool_member(stp, "enabled", true) ? "enabled" : "disabled", int_member(stp, "priority", 128), int_member(stp, "cost", 0));
  if (poe) printf("  PoE:               %s (%s)\n", bool_member(poe, "enabled", false) ? "enabled" : "disabled", string_member(poe, "mode", "at"));
  else puts("  PoE:               not supported");
}

static void usage(FILE *stream) {
  fputs("usage: postmerkosctl COMMAND [arguments]\n"
        "  session [--json|--shell] | role | has CAPABILITY\n"
        "  status | summary | ports FIRST-LAST | port PORT\n"
        "  get PATH | set PATH VALUE | set-string PATH TEXT | apply-json JSON\n"
        "  config | backup FILE | validate FILE | restore FILE | reboot\n"
        "  compatibility-needed | compatibility-report | compatibility-ack\n"
        "  users | user-create USER PASSWORD ROLE | user-role USER ROLE | user-delete USER\n"
        "  services | services-summary | services-set JSON | services-apply | service NAME ACTION\n"
        "  service-get PATH | service-set PATH VALUE\n"
        "  time | time-set JSON | time-get PATH | time-set-field PATH VALUE\n"
        "  time-sync | time-set-clock EPOCH\n", stream);
}

static int write_config_file(const char *path, struct json_object *config) {
  mode_t mask = umask(0077);
  int rc = json_object_to_file_ext(path, config, JSON_C_TO_STRING_PRETTY);
  umask(mask);
  if (rc != 0) fprintf(stderr, "postmerkosctl: unable to write %s\n", path);
  return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc < 2) { usage(stderr); return 2; }
  const char *command = argv[1];
  if (!strcmp(command, "session") || !strcmp(command, "role") || !strcmp(command, "has")) {
    struct json_object *identity = session_data();
    if (!identity) return 1;
    int rc = 0;
    if (!strcmp(command, "role")) puts(string_member(identity, "role", "none"));
    else if (!strcmp(command, "has")) {
      if (argc != 3) { usage(stderr); rc = 2; }
      else rc = capability_present(identity, argv[2]) ? 0 : 1;
    } else if (argc > 2 && !strcmp(argv[2], "--json"))
      puts(json_object_to_json_string_ext(identity, JSON_C_TO_STRING_PRETTY));
    else if (argc > 2 && !strcmp(argv[2], "--shell"))
      print_session_shell(identity);
    else printf("%s (%s)\n", string_member(identity, "username", ""), string_member(identity, "role", "none"));
    json_object_put(identity); return rc;
  }
  if (!strcmp(command, "summary") || !strcmp(command, "ports") || !strcmp(command, "port")) {
    struct json_object *snapshot = snapshot_data();
    if (!snapshot) return 1;
    int rc = 0;
    if (!strcmp(command, "summary")) print_summary(snapshot);
    else if (!strcmp(command, "ports")) {
      unsigned int first = 0, last = 0;
      if (argc != 3 || (sscanf(argv[2], "%u-%u", &first, &last) != 2 && sscanf(argv[2], "%u", &first) != 1)) { usage(stderr); rc = 2; }
      else { if (!last) last = first; print_ports(snapshot, first, last); }
    } else {
      unsigned int port = argc == 3 ? (unsigned int)strtoul(argv[2], NULL, 10) : 0;
      if (!port) { usage(stderr); rc = 2; } else print_port(snapshot, port);
    }
    json_object_put(snapshot); return rc;
  }
  if (!strcmp(command, "status") || !strcmp(command, "config")) {
    struct json_object *reply = request(!strcmp(command, "status") ? "status.get" : "config.get", NULL);
    struct json_object *data = reply_data(reply);
    if (!data) { if (reply) json_object_put(reply); return 1; }
    puts(json_object_to_json_string_ext(data, JSON_C_TO_STRING_PRETTY));
    json_object_put(reply); return 0;
  }
  if (!strcmp(command, "get")) {
    if (argc != 3) { usage(stderr); return 2; }
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "path", json_object_new_string(argv[2]));
    struct json_object *reply = request("config.path.get", data); json_object_put(data);
    struct json_object *value = reply_data(reply);
    if (!value) { if (reply) json_object_put(reply); return 1; }
    if (json_object_is_type(value, json_type_string)) puts(json_object_get_string(value));
    else puts(json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN));
    json_object_put(reply); return 0;
  }
  if (!strcmp(command, "set") || !strcmp(command, "set-string")) {
    if (argc < 4) { usage(stderr); return 2; }
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "path", json_object_new_string(argv[2]));
    json_object_object_add(data, "value", json_object_new_string(argv[3]));
    json_object_object_add(data, "string", json_object_new_boolean(!strcmp(command, "set-string")));
    struct json_object *reply = request("config.path.set", data); json_object_put(data);
    struct json_object *result = reply_data(reply);
    if (!result) { if (reply) json_object_put(reply); return 1; }
    puts(string_member(result, "message", "Configuration accepted"));
    json_object_put(reply); return 0;
  }
  if (!strcmp(command, "apply-json")) {
    if (argc != 3) { usage(stderr); return 2; }
    struct json_object *delta = json_tokener_parse(argv[2]);
    if (!delta || !json_object_is_type(delta, json_type_object)) {
      if (delta) json_object_put(delta);
      fprintf(stderr, "postmerkosctl: invalid JSON delta\n");
      return 1;
    }
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "delta", json_object_get(delta));
    struct json_object *reply = request("config.delta", data);
    json_object_put(data); json_object_put(delta);
    struct json_object *result = reply_data(reply);
    if (!result) { if (reply) json_object_put(reply); return 1; }
    puts(string_member(result, "message", "Configuration accepted"));
    json_object_put(reply); return 0;
  }
  if (!strcmp(command, "backup")) {
    if (argc != 3) { usage(stderr); return 2; }
    struct json_object *reply = request("config.get", NULL);
    struct json_object *config = reply_data(reply);
    int rc = config ? write_config_file(argv[2], config) : 1;
    if (reply) json_object_put(reply);
    return rc;
  }
  if (!strcmp(command, "validate")) {
    if (argc != 3) { usage(stderr); return 2; }
    struct json_object *config = json_object_from_file(argv[2]);
    if (!config || !json_object_is_type(config, json_type_object)) {
      if (config) json_object_put(config);
      fprintf(stderr, "postmerkosctl: invalid JSON configuration file\n");
      return 1;
    }
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "config", json_object_get(config));
    struct json_object *reply = request("config.validate", data);
    json_object_put(data); json_object_put(config);
    struct json_object *result = reply_data(reply);
    if (!result) { if (reply) json_object_put(reply); return 1; }
    puts(string_member(result, "message", "Configuration is valid"));
    json_object_put(reply); return 0;
  }
  if (!strcmp(command, "restore")) {
    if (argc != 3) { usage(stderr); return 2; }
    struct json_object *config = json_object_from_file(argv[2]);
    if (!config || !json_object_is_type(config, json_type_object)) { if (config) json_object_put(config); fprintf(stderr, "postmerkosctl: invalid configuration file\n"); return 1; }
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "config", json_object_get(config));
    struct json_object *reply = request("config.replace", data);
    json_object_put(data); json_object_put(config);
    struct json_object *result = reply_data(reply);
    if (!result) { if (reply) json_object_put(reply); return 1; }
    puts(string_member(result, "message", "Configuration restored"));
    json_object_put(reply); return 0;
  }
  if (!strcmp(command, "compatibility-needed") ||
      !strcmp(command, "compatibility-report") ||
      !strcmp(command, "compatibility-ack")) {
    const char *type = !strcmp(command, "compatibility-needed") ? "compatibility.notice" :
                       !strcmp(command, "compatibility-report") ? "compatibility.report" :
                                                                  "compatibility.ack";
    struct json_object *reply = request(type, NULL);
    struct json_object *data = reply_data(reply);
    if (!data) { if (reply) json_object_put(reply); return 1; }
    if (!strcmp(command, "compatibility-needed"))
      puts(bool_member(data, "required", false) ? "yes" : "no");
    else if (!strcmp(command, "compatibility-ack"))
      puts(string_member(data, "message", "Compatibility notice acknowledged"));
    else
      puts(json_object_to_json_string_ext(data, JSON_C_TO_STRING_PRETTY));
    json_object_put(reply);
    return 0;
  }
  if (!strcmp(command, "users")) {
    struct json_object *reply = request("users.get", NULL);
    struct json_object *data = reply_data(reply);
    if (!data) { if (reply) json_object_put(reply); return 1; }
    puts(json_object_to_json_string_ext(data, JSON_C_TO_STRING_PRETTY));
    json_object_put(reply); return 0;
  }
  if (!strcmp(command, "user-create") || !strcmp(command, "user-role") ||
      !strcmp(command, "user-delete")) {
    int needed = !strcmp(command, "user-create") ? 5 :
                 !strcmp(command, "user-role") ? 4 : 3;
    if (argc != needed) { usage(stderr); return 2; }
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "username", json_object_new_string(argv[2]));
    const char *type = NULL;
    if (!strcmp(command, "user-create")) {
      json_object_object_add(data, "password", json_object_new_string(argv[3]));
      json_object_object_add(data, "role", json_object_new_string(argv[4]));
      type = "users.create";
    } else if (!strcmp(command, "user-role")) {
      json_object_object_add(data, "role", json_object_new_string(argv[3]));
      type = "users.role";
    } else type = "users.delete";
    struct json_object *reply = request(type, data); json_object_put(data);
    struct json_object *result = reply_data(reply);
    if (!result) { if (reply) json_object_put(reply); return 1; }
    puts(json_object_to_json_string_ext(result, JSON_C_TO_STRING_PRETTY));
    json_object_put(reply); return 0;
  }
  if (!strcmp(command, "services") || !strcmp(command, "services-summary") ||
      !strcmp(command, "time")) {
    bool service_command = strcmp(command, "time") != 0;
    struct json_object *reply=request(service_command ? "services.get" : "time.get",NULL);
    struct json_object *data=reply_data(reply); if(!data){if(reply)json_object_put(reply);return 1;}
    if (!strcmp(command, "services-summary")) print_service_summary(data);
    else puts(json_object_to_json_string_ext(data,JSON_C_TO_STRING_PRETTY));
    json_object_put(reply);return 0;
  }
  if (!strcmp(command, "service-get") || !strcmp(command, "time-get")) {
    if(argc!=3){usage(stderr);return 2;}struct json_object *data=json_object_new_object();json_object_object_add(data,"path",json_object_new_string(argv[2]));
    struct json_object *reply=request(!strcmp(command,"service-get")?"services.path.get":"time.path.get",data);json_object_put(data);struct json_object *value=reply_data(reply);if(!value){if(reply)json_object_put(reply);return 1;}if(json_object_is_type(value,json_type_string))puts(json_object_get_string(value));else puts(json_object_to_json_string_ext(value,JSON_C_TO_STRING_PLAIN));json_object_put(reply);return 0;
  }
  if (!strcmp(command, "service-set") || !strcmp(command, "time-set-field")) {
    if(argc!=4){usage(stderr);return 2;}struct json_object *data=json_object_new_object();json_object_object_add(data,"path",json_object_new_string(argv[2]));json_object_object_add(data,"value",json_object_new_string(argv[3]));
    struct json_object *reply=request(!strcmp(command,"service-set")?"services.path.set":"time.path.set",data);json_object_put(data);struct json_object *result=reply_data(reply);if(!result){if(reply)json_object_put(reply);return 1;}puts(string_member(result,"message","Policy updated"));json_object_put(reply);return 0;
  }
  if (!strcmp(command, "services-apply")) {
    struct json_object *reply=request("services.apply",NULL);struct json_object *result=reply_data(reply);if(!result){if(reply)json_object_put(reply);return 1;}puts(string_member(result,"message","Service policy applied"));json_object_put(reply);return 0;
  }
  if (!strcmp(command, "services-set") || !strcmp(command, "time-set")) {
    if (argc != 3) { usage(stderr); return 2; }
    struct json_object *data = json_tokener_parse(argv[2]);
    if (!data || !json_object_is_type(data, json_type_object)) {
      if (data) json_object_put(data);
      fprintf(stderr, "postmerkosctl: invalid JSON policy\n");
      return 1;
    }
    struct json_object *reply = request(!strcmp(command, "services-set") ?
                                        "services.set" : "time.set", data);
    json_object_put(data);
    struct json_object *result = reply_data(reply);
    if (!result) { if (reply) json_object_put(reply); return 1; }
    puts(string_member(result, "message", "Policy saved"));
    json_object_put(reply);
    return 0;
  }
  if (!strcmp(command, "service")) {
    if(argc!=4){usage(stderr);return 2;}struct json_object *data=json_object_new_object();
    json_object_object_add(data,"service",json_object_new_string(argv[2]));json_object_object_add(data,"action",json_object_new_string(argv[3]));
    struct json_object *reply=request("services.action",data);json_object_put(data);struct json_object *result=reply_data(reply);if(!result){if(reply)json_object_put(reply);return 1;}puts(string_member(result,"message","Service action completed"));json_object_put(reply);return 0;
  }
  if (!strcmp(command, "time-sync")) {
    struct json_object *reply=request("time.sync",NULL);struct json_object *result=reply_data(reply);if(!result){if(reply)json_object_put(reply);return 1;}puts(string_member(result,"message","Time synchronization requested"));json_object_put(reply);return 0;
  }
  if (!strcmp(command, "time-set-clock")) {
    if (argc != 3) { usage(stderr); return 2; }
    char *end = NULL;
    long long epoch = strtoll(argv[2], &end, 10);
    if (!end || *end) {
      fprintf(stderr, "postmerkosctl: invalid epoch\n");
      return 2;
    }
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "epoch", json_object_new_int64(epoch));
    struct json_object *reply = request("time.set_clock", data);
    json_object_put(data);
    struct json_object *result = reply_data(reply);
    if (!result) { if (reply) json_object_put(reply); return 1; }
    puts(string_member(result, "message", "System clock updated"));
    json_object_put(reply);
    return 0;
  }
  if (!strcmp(command, "reboot")) {
    struct json_object *reply = request("system.reboot", NULL);
    struct json_object *data = reply_data(reply);
    if (!data) { if (reply) json_object_put(reply); return 1; }
    puts(string_member(data, "message", "Reboot scheduled"));
    json_object_put(reply); return 0;
  }
  usage(stderr); return 2;
}
