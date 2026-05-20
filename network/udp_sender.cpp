/**
 * @file udp_sender.cpp
 * @brief UDP sender implementation for moonmic
 */

#include "moonmic_internal.h"
#include "moonmic_debug.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
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
    // Initialize Winsock
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
    
    // Create UDP socket
    sender->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender->socket_fd == INVALID_SOCKET) {
        free(sender);
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }
    
    // Set non-blocking mode (optional, for better performance)
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sender->socket_fd, FIONBIO, &mode);
#else
    int flags = fcntl(sender->socket_fd, F_GETFL, 0);
    fcntl(sender->socket_fd, F_SETFL, flags | O_NONBLOCK);
#endif
    
    // Store host info
    strncpy(sender->host_ip, host_ip, sizeof(sender->host_ip) - 1);
    sender->port = port;
    sender->sequence = 0;
    
    return sender;
}

void udp_sender_destroy(udp_sender_t* sender) {
    if (!sender) {
        return;
    }
    
    if (sender->socket_fd != INVALID_SOCKET) {
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
    
#ifdef _WIN32
    dest_addr.sin_addr.s_addr = inet_addr(sender->host_ip);
#else
    inet_pton(AF_INET, sender->host_ip, &dest_addr.sin_addr);
#endif
    
    // DEBUG: Log first packet data
    static bool first_packet = true;
    if (first_packet) {
        first_packet = false;
        const uint8_t* byte_data = (const uint8_t*)data;
        MOONMIC_LOG("[udp_sender] Sending packet of size %zu", size);
        MOONMIC_LOG("[udp_sender] First 20 bytes: ");
        for (size_t i = 0; i < (size < 20 ? size : 20); i++) {
            MOONMIC_LOG("[%02zu] = 0x%02X ", i, byte_data[i]);
        }
    }

    int sent = sendto(
        sender->socket_fd,
        (const char*)data,
        (int)size,
        0,
        (struct sockaddr*)&dest_addr,
        sizeof(dest_addr)
    );
    
    return sent == (int)size;
}
