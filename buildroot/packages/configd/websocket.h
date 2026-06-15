#ifndef CONFIGD_WEBSOCKET_H
#define CONFIGD_WEBSOCKET_H

#include <json-c/json.h>
#include <libwebsockets.h>

#define MAX_CLIENTS 16
#define MAX_MSG_LEN 65536

struct lws_context *ws_init(int port);
void ws_schedule_timers(struct lws_context *context, int status_interval);
void mark_clients_pending(void);
void ws_shutdown(struct lws_context *context);
char *wrap_message(const char *type, struct json_object *data,
                   struct json_object *request_id);

#endif
