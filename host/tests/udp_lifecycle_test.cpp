#include "network/connection_monitor.h"
#include "network/udp_receiver.h"
#include "moonmic_protocol.h"

#include <atomic>
#include <chrono>
#include <thread>

int main() {
    moonmic::UDPReceiver receiver;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!receiver.start(0, "127.0.0.1")) return 1;
        if (!receiver.isRunning()) return 2;
        if (receiver.start(0, "127.0.0.1")) return 3;
        receiver.stop();
        if (receiver.isRunning()) return 4;
    }

    std::atomic<int> ping_count{0};
    receiver.setPacketCallback([&](const uint8_t* data, size_t size, const std::string&, uint16_t, bool) {
        if (size == MOONMIC_PING_SIZE && moonmic_read_u32_le(data) == MOONMIC_PING_MAGIC) {
            ping_count.fetch_add(1);
        }
    });

    if (!receiver.start(0, "127.0.0.1")) return 5;
    const uint16_t port = receiver.boundPort();
    if (port == 0) return 6;

    uint8_t ping[MOONMIC_PING_SIZE];
    moonmic_write_ping_le(ping, MOONMIC_PING_MAGIC, 1234);
    if (!receiver.sendTo(ping, sizeof(ping), "127.0.0.1", port)) return 7;

    for (int attempt = 0; attempt < 100 && ping_count.load() < 1; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (ping_count.load() < 1) return 8;

    moonmic::ConnectionMonitor monitor;
    monitor.start("127.0.0.1", port);
    for (int attempt = 0; attempt < 100 && ping_count.load() < 2; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (ping_count.load() < 2) return 9;

    const auto stop_started = std::chrono::steady_clock::now();
    monitor.stop();
    const auto stop_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - stop_started);
    if (stop_elapsed >= std::chrono::milliseconds(1000)) return 10;

    receiver.stop();
    return receiver.start(48100, "not-an-ip") ? 11 : 0;
}
