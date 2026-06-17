#ifndef CONFIGD_LOCAL_SOCKET_H
#define CONFIGD_LOCAL_SOCKET_H
int local_socket_init(const char *path);
int local_socket_service_once(int listen_fd, int timeout_ms);
void local_socket_shutdown(int listen_fd, const char *path);
#endif
