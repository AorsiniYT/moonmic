
#include "udp_receiver.h"
#include <iostream>
#include <cstring>
#include <limits>

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
#include <pthread.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

namespace moonmic {

#ifdef _WIN32
static DWORD WINAPI thread_func(LPVOID arg) {
    auto* receiver = static_cast<UDPReceiver*>(arg);
    receiver->receiveLoop();
    return 0;
}
#else
static void* thread_func(void* arg) {
    auto* receiver = static_cast<UDPReceiver*>(arg);
    receiver->receiveLoop();
    return nullptr;
}
#endif

UDPReceiver::UDPReceiver() : socket_fd_(INVALID_SOCKET), running_(false), thread_handle_(nullptr) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

UDPReceiver::~UDPReceiver() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool UDPReceiver::start(int port, const std::string& bind_address) {
    if (running_) {
        return false;
    }

    socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd_ == INVALID_SOCKET) {
        std::cerr << "[UDPReceiver] Failed to create socket" << std::endl;
        return false;
    }

    int reuse = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "[UDPReceiver] Invalid bind address: " << bind_address << std::endl;
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
        return false;
    }

    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[UDPReceiver] Failed to bind to port " << port << std::endl;
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
        return false;
    }

    running_ = true;

#ifdef _WIN32
    thread_handle_ = CreateThread(NULL, 0, thread_func, this, 0, NULL);
    if (!thread_handle_) {
        running_ = false;
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
        std::cerr << "[UDPReceiver] Failed to create receive thread" << std::endl;
        return false;
    }
#else
    pthread_t* thread = new pthread_t;
    if (pthread_create(thread, NULL, thread_func, this) != 0) {
        delete thread;
        running_ = false;
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
        std::cerr << "[UDPReceiver] Failed to create receive thread" << std::endl;
        return false;
    }
    thread_handle_ = thread;
#endif

    std::cout << "[UDPReceiver] Started on " << bind_address << ":" << port << std::endl;
    return true;
}

void UDPReceiver::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (socket_fd_ != INVALID_SOCKET) {
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
    }

    if (thread_handle_) {
#ifdef _WIN32
        WaitForSingleObject(thread_handle_, INFINITE);
        CloseHandle(thread_handle_);
#else
        pthread_t* thread = static_cast<pthread_t*>(thread_handle_);
        pthread_join(*thread, NULL);
        delete thread;
#endif
        thread_handle_ = nullptr;
    }

    std::cout << "[UDPReceiver] Stopped" << std::endl;
}

void UDPReceiver::receiveLoop() {
    uint8_t buffer[4096];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    while (running_) {
        sender_len = sizeof(sender_addr);
        int received =
            recvfrom(socket_fd_, (char*)buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &sender_len);

        if (received < 0) {
            if (running_) {
                std::cerr << "[UDPReceiver] Receive error" << std::endl;
            }
            break;
        }

        if (received < 20) {
            continue;
        }

        char sender_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
        uint16_t sender_port = ntohs(sender_addr.sin_port);

        bool is_lagging = false;
        unsigned long bytes_available = 0;

#ifdef _WIN32
        ioctlsocket(socket_fd_, FIONREAD, &bytes_available);
#else
        ioctl(socket_fd_, FIONREAD, &bytes_available);
#endif

        if (bytes_available > 2048) {
            is_lagging = true;
        }

        if (packet_callback_) {
            packet_callback_(buffer, received, std::string(sender_ip), sender_port, is_lagging);
        }
    }
}

bool UDPReceiver::sendTo(const void* data, size_t size, const std::string& ip, uint16_t port) {
    if (socket_fd_ == INVALID_SOCKET || size > static_cast<size_t>(std::numeric_limits<int>::max())) return false;

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    dest_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    int sent = sendto(socket_fd_, (const char*)data, static_cast<int>(size), 0, (struct sockaddr*)&dest_addr,
                      sizeof(dest_addr));

    return sent == (int)size;
}

} // namespace moonmic
