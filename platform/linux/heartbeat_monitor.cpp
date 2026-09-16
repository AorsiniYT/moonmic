#include "heartbeat_monitor.h"
#include "moonmic_protocol.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace {
constexpr uint64_t ping_interval_ms = 1000;
constexpr uint64_t ping_timeout_ms = 3000;

uint64_t getTimeMs() {
    timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<uint64_t>(value.tv_sec) * 1000 + value.tv_nsec / 1000000;
}
} // namespace

struct heartbeat_monitor_t {
    int socket;
    sockaddr_in destination;
    volatile int running;
    volatile int status;
    volatile int paused;
    volatile int current_rtt;
    volatile uint64_t last_packet_time;
    pthread_t thread;
};

static void* monitorThread(void* parameter) {
    auto* monitor = static_cast<heartbeat_monitor_t*>(parameter);
    uint8_t buffer[32];
    uint64_t last_sent_ping = 0;

    while (__sync_fetch_and_add(&monitor->running, 0)) {
        const uint64_t now = getTimeMs();
        if (now - last_sent_ping >= ping_interval_ms) {
            uint8_t packet[MOONMIC_PING_SIZE];
            moonmic_write_ping_le(packet, MOONMIC_PING_MAGIC, now);
            sendto(monitor->socket, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr*>(&monitor->destination),
                   sizeof(monitor->destination));
            last_sent_ping = now;
        }

        const ssize_t received = recv(monitor->socket, buffer, sizeof(buffer), 0);
        if (received >= static_cast<ssize_t>(sizeof(uint32_t))) {
            const uint32_t magic = moonmic_read_u32_le(buffer);

            if (received == MOONMIC_PING_SIZE && (magic == MOONMIC_PING_MAGIC || magic == MOONMIC_PONG_MAGIC)) {
                monitor->last_packet_time = getTimeMs();
                __sync_lock_test_and_set(&monitor->status, MOONMIC_CONNECTED);

                if (magic == MOONMIC_PING_MAGIC) {
                    uint8_t pong[MOONMIC_PING_SIZE];
                    moonmic_write_ping_le(pong, MOONMIC_PONG_MAGIC, moonmic_read_ping_timestamp_le(buffer));
                    sendto(monitor->socket, pong, sizeof(pong), 0,
                           reinterpret_cast<const sockaddr*>(&monitor->destination), sizeof(monitor->destination));
                } else {
                    const uint64_t elapsed = getTimeMs() - moonmic_read_ping_timestamp_le(buffer);
                    if (elapsed < 5000) {
                        __sync_lock_test_and_set(&monitor->current_rtt, static_cast<int>(elapsed));
                    }
                }
            } else if (received == MOONMIC_CONTROL_SIZE && magic == MOONMIC_CTRL_STOP) {
                __sync_lock_test_and_set(&monitor->paused, 1);
            } else if (received == MOONMIC_CONTROL_SIZE && magic == MOONMIC_CTRL_START) {
                __sync_lock_test_and_set(&monitor->paused, 0);
            }
        }

        if (getTimeMs() - monitor->last_packet_time > ping_timeout_ms) {
            __sync_lock_test_and_set(&monitor->status, MOONMIC_DISCONNECTED);
            __sync_lock_test_and_set(&monitor->current_rtt, -1);
        }
        usleep(10000);
    }
    return nullptr;
}

extern "C" {

heartbeat_monitor_t* heartbeat_monitor_create(intptr_t socket_fd, const char* host_ip, uint16_t host_port) {
    if (socket_fd < 0 || !host_ip) {
        return nullptr;
    }

    auto* monitor = static_cast<heartbeat_monitor_t*>(calloc(1, sizeof(heartbeat_monitor_t)));
    if (!monitor) {
        return nullptr;
    }

    monitor->socket = static_cast<int>(socket_fd);
    monitor->destination.sin_family = AF_INET;
    monitor->destination.sin_port = htons(host_port);
    if (inet_pton(AF_INET, host_ip, &monitor->destination.sin_addr) != 1) {
        free(monitor);
        return nullptr;
    }

    monitor->last_packet_time = getTimeMs();
    monitor->status = MOONMIC_DISCONNECTED;
    monitor->current_rtt = -1;
    monitor->running = 1;

    if (pthread_create(&monitor->thread, nullptr, monitorThread, monitor) != 0) {
        free(monitor);
        return nullptr;
    }
    return monitor;
}

void heartbeat_monitor_destroy(heartbeat_monitor_t* monitor) {
    if (!monitor) {
        return;
    }

    __sync_lock_test_and_set(&monitor->running, 0);
    pthread_join(monitor->thread, nullptr);
    free(monitor);
}

moonmic_connection_status_t heartbeat_monitor_get_status(heartbeat_monitor_t* monitor) {
    if (!monitor) {
        return MOONMIC_DISCONNECTED;
    }
    return static_cast<moonmic_connection_status_t>(__sync_fetch_and_add(&monitor->status, 0));
}

int heartbeat_monitor_get_rtt(heartbeat_monitor_t* monitor) {
    return monitor ? __sync_fetch_and_add(&monitor->current_rtt, 0) : -1;
}

bool heartbeat_monitor_is_connected(heartbeat_monitor_t* monitor) {
    return heartbeat_monitor_get_status(monitor) == MOONMIC_CONNECTED;
}

bool heartbeat_monitor_is_paused(heartbeat_monitor_t* monitor) {
    return monitor && __sync_fetch_and_add(&monitor->paused, 0) != 0;
}
}
