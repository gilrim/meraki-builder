#include "../configd.h"
#include "../telemetry.h"
#include <json-c/json.h>
#include <libpd690xx.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

/* Required globals for linking telemetry.c (mirroring test_network.c) */
bool dry_run = true;
const char *config_file = "/tmp/configd-telemetry-test.json";
char meraki_mac[18] = "00:11:22:33:44:55";
struct hardware_info hardware;
struct pd690xx_cfg pd690xx;

static int service_calls;
static int restart_snapshot_seen;
static char last_service[32];
static char last_action[32];

int service_action(const char *service, const char *action, char *error, size_t error_size) {
  (void)error; (void)error_size;
  service_calls++;
  snprintf(last_service, sizeof(last_service), "%s", service ? service : "");
  snprintf(last_action, sizeof(last_action), "%s", action ? action : "");
  if (service && action && !strcmp(service, "snmp") && !strcmp(action, "restart")) {
    const char *snapshot = getenv("CONFIGD_PORTSTATS_FILE");
    const char *environment = getenv("CONFIGD_SNMPD_ENV");
    restart_snapshot_seen = snapshot && environment &&
                            access(snapshot, R_OK) == 0 &&
                            access(environment, R_OK) == 0;
  }
  return 0;
}

static size_t enc_varint(unsigned char *out, uint64_t value) {
  size_t used = 0;
  do {
    unsigned char byte = (unsigned char)(value & 0x7fU);
    value >>= 7;
    if (value) byte |= 0x80U;
    out[used++] = byte;
  } while (value);
  return used;
}

static size_t put_varint(unsigned char *out, unsigned int field, uint64_t value) {
  size_t used = enc_varint(out, ((uint64_t)field << 3));
  return used + enc_varint(out + used, value);
}

static size_t put_message(unsigned char *out, unsigned int field,
                          const unsigned char *message, size_t length) {
  size_t used = enc_varint(out, ((uint64_t)field << 3) | 2U);
  used += enc_varint(out + used, length);
  memcpy(out + used, message, length);
  return used + length;
}

static size_t build_counter_fixture(unsigned char *out) {
  unsigned char port[128];
  size_t port_len = 0;
  port_len += put_varint(port + port_len, 1, 2);
  port_len += put_varint(port + port_len, 3, 1234);
  port_len += put_varint(port + port_len, 4, 12);
  port_len += put_varint(port + port_len, 13, 5678);
  port_len += put_varint(port + port_len, 14, 34);
  size_t used = put_varint(out, 2, 99);
  return used + put_message(out + used, 3, port, port_len);
}

static struct json_object *parse(const char *t) {
  struct json_object *v = json_tokener_parse(t);
  assert(v);
  return v;
}

int main(void) {
  char err[256];

  /* absent telemetry section is valid */
  struct json_object *none = parse("{\"network\":{}}");
  assert(telemetry_validate(none, err, sizeof(err)) == 0);
  json_object_put(none);

  /* defaults: both disabled, port 9100, empty community */
  struct json_object *def = telemetry_default_config();
  struct json_object *snmp, *prom, *v;
  assert(json_object_object_get_ex(def, "snmp", &snmp));
  assert(json_object_object_get_ex(snmp, "enabled", &v) && !json_object_get_boolean(v));
  assert(json_object_object_get_ex(snmp, "community", &v) &&
         strcmp(json_object_get_string(v), "") == 0);
  assert(json_object_object_get_ex(snmp, "management_only", &v) &&
         json_object_get_boolean(v));
  assert(json_object_object_get_ex(def, "prometheus", &prom));
  assert(json_object_object_get_ex(prom, "port", &v) && json_object_get_int(v) == 9100);
  assert(json_object_object_get_ex(prom, "management_only", &v) &&
         json_object_get_boolean(v));
  json_object_put(def);

  /* enabling SNMP with empty community is rejected */
  struct json_object *bad = parse(
    "{\"telemetry\":{\"snmp\":{\"enabled\":true,\"community\":\"\"}}}");
  assert(telemetry_validate(bad, err, sizeof(err)) == -EINVAL);
  json_object_put(bad);

  /* enabling SNMP with a community is accepted */
  struct json_object *ok = parse(
    "{\"telemetry\":{\"snmp\":{\"enabled\":true,\"community\":\"s3cret\"}}}");
  assert(telemetry_validate(ok, err, sizeof(err)) == 0);
  json_object_put(ok);

  /* prometheus port out of range rejected */
  struct json_object *badport = parse(
    "{\"telemetry\":{\"prometheus\":{\"enabled\":true,\"port\":70000}}}");
  assert(telemetry_validate(badport, err, sizeof(err)) == -EINVAL);
  json_object_put(badport);

  /* denylisted port 161 rejected */
  struct json_object *deny = parse(
    "{\"telemetry\":{\"prometheus\":{\"enabled\":true,\"port\":161}}}");
  assert(telemetry_validate(deny, err, sizeof(err)) == -EINVAL);
  json_object_put(deny);

  /* unknown key rejected */
  struct json_object *unknown = parse(
    "{\"telemetry\":{\"bogus\":1}}");
  assert(telemetry_validate(unknown, err, sizeof(err)) == -EINVAL);
  json_object_put(unknown);

  /* snmpd.env render */
  {
    char path[256];
    snprintf(path, sizeof(path), "%s/snmpd.env",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    setenv("CONFIGD_SNMPD_ENV", path, 1);
    struct json_object *cfg = parse(
      "{\"telemetry\":{\"snmp\":{\"enabled\":true,\"community\":\"s3cret\","
      "\"location\":\"rack 4\",\"contact\":\"net@ex\"}}}");
    assert(telemetry_write_snmpd_env(cfg, "192.0.2.10") == 0);
    json_object_put(cfg);

    FILE *f = fopen(path, "r");
    assert(f);
    char c[1024]; size_t n = fread(c, 1, sizeof(c) - 1, f); fclose(f); c[n] = '\0';
    assert(strstr(c, "SNMP_COMMUNITY='s3cret'"));
    assert(strstr(c, "SNMP_LOCATION='rack 4'"));
    assert(strstr(c, "SNMP_CONTACT='net@ex'"));
    assert(strstr(c, "SNMP_ENABLED='1'"));
    assert(strstr(c, "SNMP_BIND='192.0.2.10'"));
    assert(strstr(c, "SNMP_BIND_DEVICE='linux_mgmt'"));
    unsetenv("CONFIGD_SNMPD_ENV");
  }

  /* dump_pports may be sparse or reordered; field 1 is authoritative. */
  {
    char path[256];
    snprintf(path, sizeof(path), "%s/dump-pports.test",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    FILE *f = fopen(path, "w");
    assert(f);
    fputs("port established speed\n5 1 1000\n2 0 100\n", f);
    fclose(f);
    setenv("CONFIGD_PORTS_FILE", path, 1);
    struct portstats_snapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.valid = 1;
    snapshot.count = 2;
    snapshot.ports[1].present = 1; snapshot.ports[1].port = 2;
    snapshot.ports[4].present = 1; snapshot.ports[4].port = 5;
    telemetry_enrich_port_status(&snapshot);
    assert(snapshot.ports[4].oper == PORT_LINK_UP);
    assert(snapshot.ports[4].speed_mbps == 1000);
    assert(snapshot.ports[1].oper == PORT_LINK_DOWN);
    assert(snapshot.ports[1].speed_mbps == 100);
    unsetenv("CONFIGD_PORTS_FILE");
    unlink(path);
  }


  /* SNMP is primed from a valid counter snapshot before the daemon restart. */
  {
    char directory[256], protobuf_path[320], ports_path[320];
    char snapshot_path[320], env_path[320];
    snprintf(directory, sizeof(directory), "%s/configd-telemetry-apply-%ld",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid());
    assert(mkdir(directory, 0700) == 0 || errno == EEXIST);
    snprintf(protobuf_path, sizeof(protobuf_path), "%s/counters.bin", directory);
    snprintf(ports_path, sizeof(ports_path), "%s/dump_pports", directory);
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/portstats.v1", directory);
    snprintf(env_path, sizeof(env_path), "%s/snmpd.env", directory);

    unsigned char fixture[256];
    size_t fixture_len = build_counter_fixture(fixture);
    FILE *f = fopen(protobuf_path, "wb");
    assert(f && fwrite(fixture, 1, fixture_len, f) == fixture_len);
    fclose(f);
    f = fopen(ports_path, "w");
    assert(f);
    fputs("port established speed\n2 1 1000\n", f);
    fclose(f);

    setenv("CONFIGD_PORT_PROTOBUF", protobuf_path, 1);
    setenv("CONFIGD_PORTS_FILE", ports_path, 1);
    setenv("CONFIGD_PORTSTATS_FILE", snapshot_path, 1);
    setenv("CONFIGD_SNMPD_ENV", env_path, 1);
    dry_run = false;
    service_calls = 0;
    restart_snapshot_seen = 0;
    struct json_object *cfg = parse(
      "{\"telemetry\":{\"snmp\":{\"enabled\":true,"
      "\"community\":\"private\",\"management_only\":true},"
      "\"prometheus\":{\"enabled\":false}}}");
    struct apply_result result;
    apply_result_init(&result);
    assert(telemetry_apply(cfg, "192.0.2.10", &result) == 0);
    assert(apply_result_success(&result));
    assert(service_calls == 1);
    assert(!strcmp(last_service, "snmp") && !strcmp(last_action, "restart"));
    assert(restart_snapshot_seen);
    FILE *snapshot = fopen(snapshot_path, "r");
    assert(snapshot);
    char body[2048];
    size_t body_len = fread(body, 1, sizeof(body) - 1, snapshot);
    fclose(snapshot);
    body[body_len] = '\0';
    assert(strstr(body, "2 port2 down up 1000 1234 12"));
    apply_result_cleanup(&result);
    json_object_put(cfg);

    /* A missing or invalid proprietary counter source fails closed and does
     * not start SNMP with an empty, permanently fixed row set. */
    setenv("CONFIGD_PORT_PROTOBUF", "/nonexistent/configd-counter-source", 1);
    service_calls = 0;
    cfg = parse(
      "{\"telemetry\":{\"snmp\":{\"enabled\":true,"
      "\"community\":\"private\"}}}");
    apply_result_init(&result);
    assert(telemetry_apply(cfg, "192.0.2.10", &result) != 0);
    assert(!apply_result_success(&result));
    assert(service_calls == 0);
    apply_result_cleanup(&result);
    json_object_put(cfg);

    /* Listener bind errors are returned to the configuration transaction. */
    int blocker = socket(AF_INET, SOCK_STREAM, 0);
    assert(blocker >= 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(blocker, (struct sockaddr *)&address, sizeof(address)) == 0);
    assert(listen(blocker, 1) == 0);
    socklen_t address_len = sizeof(address);
    assert(getsockname(blocker, (struct sockaddr *)&address, &address_len) == 0);
    int blocked_port = ntohs(address.sin_port);
    char json[256];
    snprintf(json, sizeof(json),
      "{\"telemetry\":{\"snmp\":{\"enabled\":false},"
      "\"prometheus\":{\"enabled\":true,\"port\":%d,"
      "\"management_only\":false}}}", blocked_port);
    cfg = parse(json);
    apply_result_init(&result);
    assert(telemetry_apply(cfg, NULL, &result) != 0);
    assert(!apply_result_success(&result));
    apply_result_cleanup(&result);
    json_object_put(cfg);
    close(blocker);

    telemetry_shutdown();
    dry_run = true;
    unlink(protobuf_path);
    unlink(ports_path);
    unlink(snapshot_path);
    unlink(env_path);
    rmdir(directory);
    unsetenv("CONFIGD_PORT_PROTOBUF");
    unsetenv("CONFIGD_PORTS_FILE");
    unsetenv("CONFIGD_PORTSTATS_FILE");
    unsetenv("CONFIGD_SNMPD_ENV");
  }

  puts("telemetry validation tests passed");
  return 0;
}
