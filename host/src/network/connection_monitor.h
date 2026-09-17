#pragma once

#include <cstdint>
#include <string>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "moonmic_protocol.h"

namespace moonmic {

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
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    intptr_t socket_fd_;
};

} // namespace moonmic
