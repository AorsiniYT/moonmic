#include "logger.h"
#include "connection_monitor.h"
#include <iostream>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
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
    : running_(false), client_port_(0), socket_fd_(static_cast<intptr_t>(INVALID_SOCKET)) {
}

ConnectionMonitor::~ConnectionMonitor() {
    stop();
}

void ConnectionMonitor::start(const std::string& client_ip, uint16_t port) {
    if (running_) {
        moonmic::logError() << "[ConnectionMonitor] Already running" << std::endl;
        return;
    }

    client_ip_ = client_ip;
    client_port_ = port;

    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ == static_cast<intptr_t>(INVALID_SOCKET)) {
        moonmic::logError() << "[ConnectionMonitor] Failed to create socket" << std::endl;
        return;
    }

    running_ = true;
    ping_thread_ = std::thread(&ConnectionMonitor::pingThreadFunc, this);

    moonmic::logInfo() << "[ConnectionMonitor] Started pinging " << client_ip << ":" << port << std::endl;
}

void ConnectionMonitor::stop() {
    const bool was_running = running_.exchange(false);
    wait_cv_.notify_all();

    const bool had_thread = ping_thread_.joinable();
    if (had_thread) {
        ping_thread_.join();
    }

    if (socket_fd_ != static_cast<intptr_t>(INVALID_SOCKET)) {
        closesocket(socket_fd_);
        socket_fd_ = static_cast<intptr_t>(INVALID_SOCKET);
    }

    if (was_running || had_thread) {
        moonmic::logInfo() << "[ConnectionMonitor] Stopped" << std::endl;
    }
}

void ConnectionMonitor::sendPacket(const void* data, size_t size) {
    if (!running_ || socket_fd_ == static_cast<intptr_t>(INVALID_SOCKET)) {
        moonmic::logError() << "[ConnectionMonitor] Cannot send packet: not running" << std::endl;
        return;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(client_port_);
    inet_pton(AF_INET, client_ip_.c_str(), &dest_addr.sin_addr);

    const auto sent = sendto(socket_fd_, (const char*)data, size, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    if (sent < 0 || static_cast<size_t>(sent) != size) {
        moonmic::logError() << "[ConnectionMonitor] Failed to send packet (" << sent << "/" << size << " bytes)" << std::endl;
    }
}

void ConnectionMonitor::pingThreadFunc() {
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(client_port_);
    inet_pton(AF_INET, client_ip_.c_str(), &dest_addr.sin_addr);

    while (running_) {
        uint8_t ping[MOONMIC_PING_SIZE];

        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        moonmic_write_ping_le(ping, MOONMIC_PING_MAGIC,
                              std::chrono::duration_cast<std::chrono::microseconds>(duration).count());

        const auto sent =
            sendto(socket_fd_, (const char*)ping, sizeof(ping), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        if (sent != sizeof(ping)) {
            moonmic::logError() << "[ConnectionMonitor] Failed to send ping" << std::endl;
        }

        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_cv_.wait_for(lock, std::chrono::seconds(2), [this] { return !running_.load(); });
    }
}

} // namespace moonmic
