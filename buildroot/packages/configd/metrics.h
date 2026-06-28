#ifndef POSTMERKOS_METRICS_H
#define POSTMERKOS_METRICS_H
#include <stdbool.h>
#include <stddef.h>
#include "portstats.h"
#define METRICS_MAX_TEMPS 8
struct device_health {
  long uptime_seconds;
  int temp_count;
  double temps_celsius[METRICS_MAX_TEMPS];
  char temp_labels[METRICS_MAX_TEMPS][32];
  int poe_available;
  double poe_power_watts;
  double poe_budget_watts;
};
int metrics_render(char *buf, size_t n, const struct portstats_snapshot *snap,
                   const struct device_health *health);
int metrics_server_start(const char *bind_addr, int port);
int metrics_server_start_bound(const char *bind_addr, const char *bind_device,
                               int port, char *error, size_t error_size);
void metrics_server_stop(void);
int metrics_server_running(void);
int metrics_server_fd(void);
bool metrics_server_matches(const char *bind_addr, const char *bind_device, int port);
void metrics_server_service(const struct portstats_snapshot *snap,
                            const struct device_health *health);
#endif
