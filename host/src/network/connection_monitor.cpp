#include "connection_monitor.h"
#include <iostream>
#include <chrono>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

namespace moonmic {

ConnectionMonitor::ConnectionMonitor()
    : running_(false), client_port_(0), socket_fd_(INVALID_SOCKET) {
}

ConnectionMonitor::~ConnectionMonitor() {
    stop();
}

void ConnectionMonitor::start(const std::string& client_ip, uint16_t port) {
    if (running_) {
        std::cerr << "[ConnectionMonitor] Already running" << std::endl;
        return;
    }

    client_ip_ = client_ip;
    client_port_ = port;

    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ == INVALID_SOCKET) {
        std::cerr << "[ConnectionMonitor] Failed to create socket" << std::endl;
        return;
    }

    running_ = true;
    ping_thread_ = std::thread(&ConnectionMonitor::pingThreadFunc, this);

    std::cout << "[ConnectionMonitor] Started pinging " << client_ip << ":" << port << std::endl;
}

void ConnectionMonitor::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (ping_thread_.joinable()) {
        ping_thread_.join();
    }

    if (socket_fd_ != INVALID_SOCKET) {
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
    }

    std::cout << "[ConnectionMonitor] Stopped" << std::endl;
}

void ConnectionMonitor::sendPacket(const void* data, size_t size) {
    if (!running_ || socket_fd_ == INVALID_SOCKET) {
        std::cerr << "[ConnectionMonitor] Cannot send packet: not running" << std::endl;
        return;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(client_port_);
    inet_pton(AF_INET, client_ip_.c_str(), &dest_addr.sin_addr);

    ssize_t sent = sendto(socket_fd_, (const char*)data, size, 0,
                          (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    if (sent != (ssize_t)size) {
        std::cerr << "[ConnectionMonitor] Failed to send packet (" << sent << "/" << size << " bytes)" << std::endl;
    }
}

void ConnectionMonitor::pingThreadFunc() {
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(client_port_);
    inet_pton(AF_INET, client_ip_.c_str(), &dest_addr.sin_addr);

    while (running_) {

        MoonmicPing ping;
        ping.magic = 0x50494E47;

        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        ping.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

        ssize_t sent = sendto(socket_fd_, (const char*)&ping, sizeof(ping), 0,
                              (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        if (sent != sizeof(ping)) {
            std::cerr << "[ConnectionMonitor] Failed to send ping" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

}
