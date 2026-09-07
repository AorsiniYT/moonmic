
#include "moonmic_internal.h"
#include "moonmic_debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

udp_sender_t* udp_sender_create(const char* host_ip, uint16_t port) {
    if (!host_ip) {
        return NULL;
    }

#ifdef _WIN32

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return NULL;
    }
#endif

    udp_sender_t* sender = (udp_sender_t*)calloc(1, sizeof(udp_sender_t));
    if (!sender) {
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }

    sender->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender->socket_fd == static_cast<intptr_t>(INVALID_SOCKET)) {
        free(sender);
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sender->socket_fd, FIONBIO, &mode);
#else
    int flags = fcntl(sender->socket_fd, F_GETFL, 0);
    fcntl(sender->socket_fd, F_SETFL, flags | O_NONBLOCK);
#endif

    snprintf(sender->host_ip, sizeof(sender->host_ip), "%s", host_ip);
    sender->port = port;
    sender->sequence = 0;

    return sender;
}

void udp_sender_destroy(udp_sender_t* sender) {
    if (!sender) {
        return;
    }

    if (sender->socket_fd != static_cast<intptr_t>(INVALID_SOCKET)) {
#ifdef _WIN32
        closesocket(sender->socket_fd);
        WSACleanup();
#else
        close(sender->socket_fd);
#endif
    }

    free(sender);
}

bool udp_sender_send(udp_sender_t* sender, const void* data, size_t size) {
    if (!sender || !data || size == 0) {
        return false;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(sender->port);

    if (inet_pton(AF_INET, sender->host_ip, &dest_addr.sin_addr) != 1) {
        return false;
    }

    int sent =
        sendto(sender->socket_fd, (const char*)data, (int)size, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    return sent == (int)size;
}
