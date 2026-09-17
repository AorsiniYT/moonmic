#include "logger.h"

#include "udp_receiver.h"
#include <climits>
#include <iostream>
#include <system_error>

#ifndef _WIN32
#include <sys/ioctl.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

namespace moonmic {

UDPReceiver::UDPReceiver() : socket_fd_(INVALID_SOCKET), running_(false) {
#ifdef _WIN32
    WSADATA wsa;
    winsock_ready_ = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#endif
}

UDPReceiver::~UDPReceiver() {
    stop();
#ifdef _WIN32
    if (winsock_ready_) WSACleanup();
#endif
}

bool UDPReceiver::start(int port, const std::string& bind_address) {
    if (running_) {
        return false;
    }
#ifdef _WIN32
    if (!winsock_ready_) {
        moonmic::logError() << "[UDPReceiver] Winsock initialization failed" << std::endl;
        return false;
    }
#endif
    if (port < 0 || port > 65535) {
        moonmic::logError() << "[UDPReceiver] Invalid port: " << port << std::endl;
        return false;
    }
    if (receive_thread_.joinable()) {
        stop();
    }

    const socket_t new_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (new_socket == INVALID_SOCKET) {
        moonmic::logError() << "[UDPReceiver] Failed to create socket" << std::endl;
        return false;
    }
    socket_fd_ = new_socket;

    int reuse = 1;
    if (setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) ==
        SOCKET_ERROR) {
        moonmic::logError() << "[UDPReceiver] Failed to configure address reuse" << std::endl;
        closeSocket();
        return false;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
        moonmic::logError() << "[UDPReceiver] Invalid bind address: " << bind_address << std::endl;
        closeSocket();
        return false;
    }

    if (bind(new_socket, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        moonmic::logError() << "[UDPReceiver] Failed to bind to port " << port << std::endl;
        closeSocket();
        return false;
    }

    running_ = true;
    try {
        receive_thread_ = std::thread(&UDPReceiver::receiveLoop, this);
    } catch (const std::system_error& error) {
        running_ = false;
        closeSocket();
        moonmic::logError() << "[UDPReceiver] Failed to create receive thread: " << error.what() << std::endl;
        return false;
    }

    moonmic::logInfo() << "[UDPReceiver] Started on " << bind_address << ":" << port << std::endl;
    return true;
}

void UDPReceiver::stop() {
    const bool was_running = running_.exchange(false);
    closeSocket();
    const bool had_thread = receive_thread_.joinable();
    if (had_thread) receive_thread_.join();
    if (was_running || had_thread) moonmic::logInfo() << "[UDPReceiver] Stopped" << std::endl;
}

void UDPReceiver::closeSocket() {
    const socket_t socket = socket_fd_.exchange(INVALID_SOCKET);
    if (socket != INVALID_SOCKET) closesocket(socket);
}

void UDPReceiver::receiveLoop() {
    const socket_t socket = socket_fd_.load();
    uint8_t buffer[4096];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    while (running_) {
        sender_len = sizeof(sender_addr);
        int received = recvfrom(socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                reinterpret_cast<sockaddr*>(&sender_addr), &sender_len);

        if (received < 0) {
            if (running_) {
                moonmic::logError() << "[UDPReceiver] Receive error" << std::endl;
            }
            break;
        }

        char sender_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
        uint16_t sender_port = ntohs(sender_addr.sin_port);

        bool is_lagging = false;
        unsigned long bytes_available = 0;

#ifdef _WIN32
        ioctlsocket(socket, FIONREAD, &bytes_available);
#else
        ioctl(socket, FIONREAD, &bytes_available);
#endif

        if (bytes_available > 2048) {
            is_lagging = true;
        }

        if (packet_callback_) {
            packet_callback_(buffer, received, std::string(sender_ip), sender_port, is_lagging);
        }
    }
    running_ = false;
}

uint16_t UDPReceiver::boundPort() const {
    const socket_t socket = socket_fd_.load();
    if (socket == INVALID_SOCKET) return 0;

    struct sockaddr_in addr = {};
#ifdef _WIN32
    int addr_len = sizeof(addr);
#else
    socklen_t addr_len = sizeof(addr);
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&addr), &addr_len) == SOCKET_ERROR) return 0;
    return ntohs(addr.sin_port);
}

bool UDPReceiver::sendTo(const void* data, size_t size, const std::string& ip, uint16_t port) {
    const socket_t socket = socket_fd_.load();
    if (socket == INVALID_SOCKET || size > static_cast<size_t>(INT_MAX)) return false;

    struct sockaddr_in dest_addr = {};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &dest_addr.sin_addr) != 1) return false;

    int sent = sendto(socket, reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                      reinterpret_cast<const sockaddr*>(&dest_addr), sizeof(dest_addr));

    return sent == static_cast<int>(size);
}

} // namespace moonmic
