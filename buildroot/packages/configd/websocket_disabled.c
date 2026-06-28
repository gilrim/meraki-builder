#include "websocket.h"

#include <stdlib.h>

struct lws_context *ws_init(int port) {
  (void)port;
  return NULL;
}

void ws_schedule_timers(struct lws_context *context, int status_interval) {
  (void)context;
  (void)status_interval;
}

void mark_clients_pending(void) {}

int ws_service_once(struct lws_context *context, int timeout_ms) {
  (void)context;
  (void)timeout_ms;
  return -1;
}

void ws_shutdown(struct lws_context *context) { (void)context; }
void ws_revoke_user_sessions(const char *username) { (void)username; }

char *wrap_message(const char *type, struct json_object *data,
                   struct json_object *request_id) {
  (void)type;
  (void)data;
  (void)request_id;
  return NULL;
}
