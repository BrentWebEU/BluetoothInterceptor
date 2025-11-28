#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>
#include <stddef.h>

int tcp_create_server(uint16_t port);
int tcp_accept_client(int server_sock);
int tcp_send_data(int client_sock, const uint8_t *data, size_t len);

#endif
