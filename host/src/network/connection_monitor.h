#pragma once

#include <cstdint>
#include <string>
#include <atomic>
#include <thread>

namespace moonmic {

#pragma pack(push, 1)
struct MoonmicPing {
    uint32_t magic;
    uint64_t timestamp;
};
#pragma pack(pop)

class ConnectionMonitor {
public:
    ConnectionMonitor();
    ~ConnectionMonitor();

    void start(const std::string& client_ip, uint16_t port);

    void stop();

    bool isRunning() const { return running_; }

    void sendPacket(const void* data, size_t size);

private:
    void pingThreadFunc();

    std::atomic<bool> running_;
    std::string client_ip_;
    uint16_t client_port_;
    std::thread ping_thread_;
    int socket_fd_;
};

}
