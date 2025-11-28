#include "tcp_server.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int tcp_create_server(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ERROR_PRINT("Failed to create TCP socket");
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ERROR_PRINT("Failed to set socket options");
        close(sock);
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ERROR_PRINT("Failed to bind TCP socket on port %d", port);
        close(sock);
        return -1;
    }
    
    if (listen(sock, BACKLOG) < 0) {
        ERROR_PRINT("Failed to listen on TCP socket");
        close(sock);
        return -1;
    }
    
    INFO_PRINT("TCP server listening on port %d", port);
    return sock;
}

int tcp_accept_client(int server_sock) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
    if (client_sock < 0) {
        ERROR_PRINT("Failed to accept TCP connection");
        return -1;
    }
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    INFO_PRINT("TCP client connected: %s:%d", client_ip, ntohs(client_addr.sin_port));
    
    return client_sock;
}

int tcp_send_data(int client_sock, const uint8_t *data, size_t len) {
    ssize_t sent = send(client_sock, data, len, 0);
    if (sent < 0) {
        ERROR_PRINT("Failed to send data to TCP client");
        return -1;
    }
    
    if ((size_t)sent != len) {
        DEBUG_PRINT("Partial send: %zd/%zu bytes", sent, len);
    }
    
    return sent;
}
