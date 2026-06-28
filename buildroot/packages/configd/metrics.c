#include "metrics.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

struct sink {
  char *buf;
  size_t cap;
  size_t len;
  int overflow;
};

static void emit(struct sink *s, const char *fmt, ...) {
  if (s->overflow) return;
  va_list ap;
  va_start(ap, fmt);
  int avail = (int)(s->cap - s->len);
  int wrote = vsnprintf(s->buf + s->len, (size_t)avail, fmt, ap);
  va_end(ap);
  if (wrote < 0 || wrote >= avail) { s->overflow = 1; return; }
  s->len += (size_t)wrote;
}

int metrics_render(char *buf, size_t n,
                   const struct portstats_snapshot *snap,
                   const struct device_health *health) {
  struct sink s = { buf, n, 0, 0 };
  int ok = snap && snap->valid;

  emit(&s, "# HELP postmerkos_scrape_success Whether the portstats scrape succeeded.\n");
  emit(&s, "# TYPE postmerkos_scrape_success gauge\n");
  emit(&s, "postmerkos_scrape_success %d\n", ok ? 1 : 0);

  if (ok) {
    emit(&s, "# HELP postmerkos_portstats_timestamp_seconds Click source timestamp.\n");
    emit(&s, "# TYPE postmerkos_portstats_timestamp_seconds gauge\n");
    emit(&s, "postmerkos_portstats_timestamp_seconds %ld\n", snap->timestamp);
    emit(&s, "# HELP postmerkos_portstats_discontinuity_seconds Last counter-reset tick.\n");
    emit(&s, "# TYPE postmerkos_portstats_discontinuity_seconds gauge\n");
    emit(&s, "postmerkos_portstats_discontinuity_seconds %ld\n", snap->discontinuity_ticks);

    emit(&s, "# HELP postmerkos_port_receive_bytes_total Received octets per port.\n");
    emit(&s, "# TYPE postmerkos_port_receive_bytes_total counter\n");
    for (int i = 0; i < PORTSTATS_MAX_PORTS; i++) {
      const struct port_counters *p = &snap->ports[i];
      if (!p->present) continue;
      emit(&s, "postmerkos_port_receive_bytes_total{port=\"%d\",ifname=\"port%d\"} %llu\n",
           p->port, p->port, (unsigned long long)p->rx_octets);
    }
    emit(&s, "# HELP postmerkos_port_transmit_bytes_total Transmitted octets per port.\n");
    emit(&s, "# TYPE postmerkos_port_transmit_bytes_total counter\n");
    for (int i = 0; i < PORTSTATS_MAX_PORTS; i++) {
      const struct port_counters *p = &snap->ports[i];
      if (!p->present) continue;
      emit(&s, "postmerkos_port_transmit_bytes_total{port=\"%d\",ifname=\"port%d\"} %llu\n",
           p->port, p->port, (unsigned long long)p->tx_octets);
    }
    emit(&s, "# HELP postmerkos_port_receive_packets_total Received packets per port.\n");
    emit(&s, "# TYPE postmerkos_port_receive_packets_total counter\n");
    for (int i = 0; i < PORTSTATS_MAX_PORTS; i++) {
      const struct port_counters *p = &snap->ports[i];
      if (!p->present) continue;
      emit(&s, "postmerkos_port_receive_packets_total{port=\"%d\",ifname=\"port%d\"} %llu\n",
           p->port, p->port, (unsigned long long)p->rx_packets);
    }
    emit(&s, "# HELP postmerkos_port_transmit_packets_total Transmitted packets per port.\n");
    emit(&s, "# TYPE postmerkos_port_transmit_packets_total counter\n");
    for (int i = 0; i < PORTSTATS_MAX_PORTS; i++) {
      const struct port_counters *p = &snap->ports[i];
      if (!p->present) continue;
      emit(&s, "postmerkos_port_transmit_packets_total{port=\"%d\",ifname=\"port%d\"} %llu\n",
           p->port, p->port, (unsigned long long)p->tx_packets);
    }
    emit(&s, "# HELP postmerkos_port_up Operational link state per port.\n");
    emit(&s, "# TYPE postmerkos_port_up gauge\n");
    for (int i = 0; i < PORTSTATS_MAX_PORTS; i++) {
      const struct port_counters *p = &snap->ports[i];
      if (!p->present) continue;
      emit(&s, "postmerkos_port_up{port=\"%d\",ifname=\"port%d\"} %d\n",
           p->port, p->port, p->oper == PORT_LINK_UP ? 1 : 0);
    }
    emit(&s, "# HELP postmerkos_port_speed_mbps Negotiated link speed per port.\n");
    emit(&s, "# TYPE postmerkos_port_speed_mbps gauge\n");
    for (int i = 0; i < PORTSTATS_MAX_PORTS; i++) {
      const struct port_counters *p = &snap->ports[i];
      if (!p->present) continue;
      emit(&s, "postmerkos_port_speed_mbps{port=\"%d\",ifname=\"port%d\"} %d\n",
           p->port, p->port, p->speed_mbps);
    }
    /* per-port PoE, only for ports with a valid reading */
    int any_poe = 0;
    for (int i = 0; i < PORTSTATS_MAX_PORTS; i++)
      if (snap->ports[i].present && snap->ports[i].poe_present) { any_poe = 1; break; }
    if (any_poe) {
      emit(&s, "# HELP postmerkos_poe_port_power_watts Per-port PoE power draw.\n");
      emit(&s, "# TYPE postmerkos_poe_port_power_watts gauge\n");
      for (int i = 0; i < PORTSTATS_MAX_PORTS; i++) {
        const struct port_counters *p = &snap->ports[i];
        if (!p->present || !p->poe_present) continue;
        emit(&s, "postmerkos_poe_port_power_watts{port=\"%d\",ifname=\"port%d\"} %.3f\n",
             p->port, p->port, p->poe_power_watts);
      }
    }
  }

  if (health) {
    emit(&s, "# HELP postmerkos_uptime_seconds System uptime.\n");
    emit(&s, "# TYPE postmerkos_uptime_seconds gauge\n");
    emit(&s, "postmerkos_uptime_seconds %ld\n", health->uptime_seconds);
    if (health->temp_count > 0) {
      emit(&s, "# HELP postmerkos_temperature_celsius Temperature sensors.\n");
      emit(&s, "# TYPE postmerkos_temperature_celsius gauge\n");
      for (int i = 0; i < health->temp_count && i < METRICS_MAX_TEMPS; i++)
        emit(&s, "postmerkos_temperature_celsius{sensor=\"%s\"} %g\n",
             health->temp_labels[i], health->temps_celsius[i]);
    }
    if (health->poe_available) {
      emit(&s, "# HELP postmerkos_poe_power_watts Aggregate PoE power consumption.\n");
      emit(&s, "# TYPE postmerkos_poe_power_watts gauge\n");
      emit(&s, "postmerkos_poe_power_watts %.3f\n", health->poe_power_watts);
      emit(&s, "# HELP postmerkos_poe_budget_watts Aggregate PoE power budget.\n");
      emit(&s, "# TYPE postmerkos_poe_budget_watts gauge\n");
      emit(&s, "postmerkos_poe_budget_watts %.3f\n", health->poe_budget_watts);
    }
  }

  if (s.overflow) return -1;
  return (int)s.len;
}

#define METRICS_BODY_MAX (256 * 1024)
#define METRICS_REQUEST_MAX 4096
#define METRICS_CLIENT_MAX 8
#define METRICS_ACCEPT_BUDGET 4
#define METRICS_WRITE_BUDGET (16 * 1024)
#define METRICS_CONN_TIMEOUT_MS 2000

struct metrics_client {
  int fd;
  char request[METRICS_REQUEST_MAX];
  size_t request_len;
  char *response;
  size_t response_len;
  size_t response_off;
  long deadline_ms;
};

static int g_listen_fd = -1;
static int g_port;
static char g_bind_addr[64];
static char g_bind_device[64];
static struct metrics_client g_clients[METRICS_CLIENT_MAX];
static int g_initialized;

static long now_monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void initialize_clients(void) {
  if (g_initialized) return;
  for (size_t i = 0; i < METRICS_CLIENT_MAX; i++) g_clients[i].fd = -1;
  g_initialized = 1;
}

static void client_reset(struct metrics_client *client) {
  if (client->fd >= 0) close(client->fd);
  free(client->response);
  memset(client, 0, sizeof(*client));
  client->fd = -1;
}

int metrics_server_fd(void) { return g_listen_fd; }
int metrics_server_running(void) { return g_listen_fd >= 0; }

bool metrics_server_matches(const char *bind_addr, const char *bind_device, int port) {
  return g_listen_fd >= 0 && g_port == port &&
         !strcmp(g_bind_addr, bind_addr ? bind_addr : "") &&
         !strcmp(g_bind_device, bind_device ? bind_device : "");
}

static int create_listener(const char *bind_addr, const char *bind_device,
                           int port, char *error, size_t error_size) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) goto failed;
  int one = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_BINDTODEVICE
  if (bind_device && *bind_device &&
      setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, bind_device,
                 strlen(bind_device) + 1) != 0) goto close_failed;
#else
  if (bind_device && *bind_device) { errno = ENOTSUP; goto close_failed; }
#endif
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  if (!bind_addr || !*bind_addr) address.sin_addr.s_addr = htonl(INADDR_ANY);
  else if (inet_pton(AF_INET, bind_addr, &address.sin_addr) != 1) { errno = EINVAL; goto close_failed; }
  if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
      listen(fd, METRICS_CLIENT_MAX) != 0) goto close_failed;
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) goto close_failed;
  return fd;
close_failed: {
    int saved = errno; close(fd); errno = saved;
  }
failed:
  if (error && error_size) snprintf(error, error_size, "%s", strerror(errno));
  return -errno;
}

void metrics_server_stop(void) {
  initialize_clients();
  if (g_listen_fd >= 0) close(g_listen_fd);
  g_listen_fd = -1; g_port = 0; g_bind_addr[0] = '\0'; g_bind_device[0] = '\0';
  for (size_t i = 0; i < METRICS_CLIENT_MAX; i++) client_reset(&g_clients[i]);
}

int metrics_server_start_bound(const char *bind_addr, const char *bind_device,
                               int port, char *error, size_t error_size) {
  initialize_clients();
  if (metrics_server_matches(bind_addr, bind_device, port)) return 0;
  char old_addr[64], old_device[64]; int old_port = g_port;
  bool had_listener = g_listen_fd >= 0;
  snprintf(old_addr, sizeof(old_addr), "%s", g_bind_addr);
  snprintf(old_device, sizeof(old_device), "%s", g_bind_device);
  if (had_listener && old_port == port) metrics_server_stop();
  int candidate = create_listener(bind_addr, bind_device, port, error, error_size);
  if (candidate < 0) {
    if (had_listener && old_port == port) {
      int restored = create_listener(old_addr, old_device, old_port, NULL, 0);
      if (restored >= 0) {
        g_listen_fd = restored; g_port = old_port;
        snprintf(g_bind_addr, sizeof(g_bind_addr), "%s", old_addr);
        snprintf(g_bind_device, sizeof(g_bind_device), "%s", old_device);
      }
    }
    return candidate;
  }
  if (g_listen_fd >= 0) metrics_server_stop();
  g_listen_fd = candidate; g_port = port;
  snprintf(g_bind_addr, sizeof(g_bind_addr), "%s", bind_addr ? bind_addr : "");
  snprintf(g_bind_device, sizeof(g_bind_device), "%s", bind_device ? bind_device : "");
  return 0;
}

int metrics_server_start(const char *bind_addr, int port) {
  return metrics_server_start_bound(bind_addr, NULL, port, NULL, 0);
}

static struct metrics_client *free_slot(void) {
  for (size_t i = 0; i < METRICS_CLIENT_MAX; i++) if (g_clients[i].fd < 0) return &g_clients[i];
  return NULL;
}

static void accept_clients(void) {
  for (int count = 0; g_listen_fd >= 0 && count < METRICS_ACCEPT_BUDGET; count++) {
    int fd = accept(g_listen_fd, NULL, NULL);
    if (fd < 0) { if (errno == EINTR) { count--; continue; } break; }
    struct metrics_client *client = free_slot();
    if (!client) { close(fd); continue; }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) { close(fd); continue; }
    client->fd = fd; client->deadline_ms = now_monotonic_ms() + METRICS_CONN_TIMEOUT_MS;
  }
}

static bool headers_complete(const char *request) {
  return strstr(request, "\r\n\r\n") || strstr(request, "\n\n");
}

static int prepare_response(struct metrics_client *client,
                            const struct portstats_snapshot *snap,
                            const struct device_health *health) {
  const char *status = "404 Not Found", *content_type = "text/plain";
  char *body = NULL; int body_len = 0;
  if (!strncmp(client->request, "GET /metrics ", 13) || !strcmp(client->request, "GET /metrics")) {
    body = malloc(METRICS_BODY_MAX); if (!body) return -ENOMEM;
    body_len = metrics_render(body, METRICS_BODY_MAX, snap, health);
    if (body_len < 0) {
      free(body);
      body = strdup("metrics response exceeded the configured limit\n");
      if (!body) return -ENOMEM;
      body_len = (int)strlen(body);
      status = "500 Internal Server Error";
    } else status = "200 OK";
    content_type = "text/plain; version=0.0.4";
  } else {
    body = strdup("not found\n");
    if (!body) return -ENOMEM;
    body_len = (int)strlen(body);
  }
  char header[320];
  int header_len = snprintf(header, sizeof(header),
    "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
    status, content_type, body_len);
  if (header_len < 0 || (size_t)header_len >= sizeof(header)) { free(body); return -EOVERFLOW; }
  client->response = malloc((size_t)header_len + (size_t)body_len);
  if (!client->response) { free(body); return -ENOMEM; }
  memcpy(client->response, header, (size_t)header_len);
  memcpy(client->response + header_len, body, (size_t)body_len);
  client->response_len = (size_t)header_len + (size_t)body_len;
  free(body); return 0;
}

static void service_client(struct metrics_client *client,
                           const struct portstats_snapshot *snap,
                           const struct device_health *health) {
  if (client->fd < 0) return;
  if (now_monotonic_ms() >= client->deadline_ms) { client_reset(client); return; }
  if (!client->response) {
    for (;;) {
      if (client->request_len + 1 >= sizeof(client->request)) { client_reset(client); return; }
      ssize_t got = recv(client->fd, client->request + client->request_len,
                         sizeof(client->request) - client->request_len - 1, 0);
      if (got > 0) {
        client->request_len += (size_t)got; client->request[client->request_len] = '\0';
        if (headers_complete(client->request)) break;
        continue;
      }
      if (got == 0) { client_reset(client); return; }
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      client_reset(client); return;
    }
    char *line_end = strpbrk(client->request, "\r\n"); if (line_end) *line_end = '\0';
    if (prepare_response(client, snap, health) != 0) { client_reset(client); return; }
  }
  size_t remaining = client->response_len - client->response_off;
  size_t budget = remaining > METRICS_WRITE_BUDGET ? METRICS_WRITE_BUDGET : remaining;
  ssize_t wrote = send(client->fd, client->response + client->response_off, budget, MSG_NOSIGNAL);
  if (wrote > 0) {
    client->response_off += (size_t)wrote;
    if (client->response_off == client->response_len) { (void)shutdown(client->fd, SHUT_WR); client_reset(client); }
    return;
  }
  if (wrote < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return;
  client_reset(client);
}

void metrics_server_service(const struct portstats_snapshot *snap,
                            const struct device_health *health) {
  initialize_clients();
  if (g_listen_fd < 0) return;
  accept_clients();
  for (size_t i = 0; i < METRICS_CLIENT_MAX; i++) service_client(&g_clients[i], snap, health);
}
