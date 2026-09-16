
#pragma once

#include <stdint.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace moonmic {

class UDPReceiver {
  public:
    using PacketCallback = std::function<void(const uint8_t* data, size_t size, const std::string& sender_ip,
                                              uint16_t sender_port, bool is_lagging)>;

    UDPReceiver();
    ~UDPReceiver();

    bool start(int port, const std::string& bind_address = "0.0.0.0");
    void stop();
    bool isRunning() const { return running_; }

    void setPacketCallback(PacketCallback callback) { packet_callback_ = callback; }

    bool sendTo(const void* data, size_t size, const std::string& ip, uint16_t port);

  private:
#ifdef _WIN32
    using socket_t = unsigned long long;
#else
    using socket_t = int;
#endif

    std::atomic<socket_t> socket_fd_;
    std::atomic<bool> running_;
    std::thread receive_thread_;
    PacketCallback packet_callback_;
#ifdef _WIN32
    bool winsock_ready_ = false;
#endif

    void receiveLoop();
    void closeSocket();
};

} // namespace moonmic
