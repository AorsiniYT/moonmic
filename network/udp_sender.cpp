
#include "moonmic_internal.h"
#include "moonmic_debug.h"
#include <climits>
#include <new>

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

struct udp_sender_t {
    intptr_t socket_fd = static_cast<intptr_t>(INVALID_SOCKET);
    sockaddr_in destination = {};
    uint32_t sequence = 0;
};

static void close_sender_socket(udp_sender_t* sender) {
    if (sender->socket_fd == static_cast<intptr_t>(INVALID_SOCKET)) {
        return;
    }

#ifdef _WIN32
    closesocket(sender->socket_fd);
#else
    close(sender->socket_fd);
#endif
    sender->socket_fd = static_cast<intptr_t>(INVALID_SOCKET);
}

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

    auto* sender = new (std::nothrow) udp_sender_t();
    if (!sender) {
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }

    sender->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender->socket_fd == static_cast<intptr_t>(INVALID_SOCKET)) {
        delete sender;
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }

#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(sender->socket_fd, FIONBIO, &mode) == SOCKET_ERROR) {
        close_sender_socket(sender);
        delete sender;
        WSACleanup();
        return NULL;
    }
#else
    int flags = fcntl(sender->socket_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(sender->socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        close_sender_socket(sender);
        delete sender;
        return NULL;
    }
#endif

    sender->destination.sin_family = AF_INET;
    sender->destination.sin_port = htons(port);
    if (inet_pton(AF_INET, host_ip, &sender->destination.sin_addr) != 1) {
        close_sender_socket(sender);
        delete sender;
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }

    return sender;
}

void udp_sender_destroy(udp_sender_t* sender) {
    if (!sender) {
        return;
    }

    close_sender_socket(sender);
#ifdef _WIN32
    WSACleanup();
#endif
    delete sender;
}

bool udp_sender_send(udp_sender_t* sender, const void* data, size_t size) {
    if (!sender || !data || size == 0 || size > static_cast<size_t>(INT_MAX)) {
        return false;
    }

    const int sent = sendto(sender->socket_fd, reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                            reinterpret_cast<const sockaddr*>(&sender->destination), sizeof(sender->destination));

    return sent == static_cast<int>(size);
}

uint32_t udp_sender_next_sequence(udp_sender_t* sender) {
    return sender ? sender->sequence++ : 0;
}

intptr_t udp_sender_socket(const udp_sender_t* sender) {
    return sender ? sender->socket_fd : static_cast<intptr_t>(INVALID_SOCKET);
}
