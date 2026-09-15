#include "heartbeat_monitor.h"
#include "moonmic_protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdlib>
#include <cstring>

struct heartbeat_monitor_t {
    SOCKET socket;
    sockaddr_in destination;
    volatile LONG running;
    volatile LONG status;
    volatile LONG paused;
    volatile LONG current_rtt;
    volatile ULONGLONG last_packet_time;
    HANDLE thread_handle;
};

namespace {
constexpr ULONGLONG ping_interval_ms = 1000;
constexpr ULONGLONG ping_timeout_ms = 3000;

ULONGLONG getTimeMs() {
    return GetTickCount64();
}

DWORD WINAPI monitorThread(LPVOID parameter) {
    auto* monitor = static_cast<heartbeat_monitor_t*>(parameter);
    uint8_t buffer[32];
    ULONGLONG last_sent_ping = 0;

    while (InterlockedCompareExchange(&monitor->running, 0, 0)) {
        const ULONGLONG now = getTimeMs();
        if (now - last_sent_ping >= ping_interval_ms) {
            uint8_t packet[sizeof(moonmic_ping_packet_t)];
            moonmic_write_ping_le(packet, MOONMIC_PING_MAGIC, now);
            sendto(monitor->socket, reinterpret_cast<const char*>(packet), sizeof(packet), 0,
                   reinterpret_cast<const sockaddr*>(&monitor->destination), sizeof(monitor->destination));
            last_sent_ping = now;
        }

        const int received = recv(monitor->socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
        if (received >= static_cast<int>(sizeof(uint32_t))) {
            const uint32_t magic = moonmic_read_u32_le(buffer);

            if (received == sizeof(moonmic_ping_packet_t) &&
                (magic == MOONMIC_PING_MAGIC || magic == MOONMIC_PONG_MAGIC)) {
                monitor->last_packet_time = getTimeMs();
                InterlockedExchange(&monitor->status, MOONMIC_CONNECTED);

                if (magic == MOONMIC_PING_MAGIC) {
                    uint8_t pong[sizeof(moonmic_ping_packet_t)];
                    moonmic_write_ping_le(pong, MOONMIC_PONG_MAGIC, moonmic_read_ping_timestamp_le(buffer));
                    sendto(monitor->socket, reinterpret_cast<const char*>(pong), sizeof(pong), 0,
                           reinterpret_cast<const sockaddr*>(&monitor->destination), sizeof(monitor->destination));
                } else {
                    const ULONGLONG elapsed = getTimeMs() - (ULONGLONG)moonmic_read_ping_timestamp_le(buffer);
                    if (elapsed < 5000) {
                        InterlockedExchange(&monitor->current_rtt, static_cast<LONG>(elapsed));
                    }
                }
            } else if (received == sizeof(moonmic_control_packet_t) && magic == MOONMIC_CTRL_STOP) {
                InterlockedExchange(&monitor->paused, 1);
            } else if (received == sizeof(moonmic_control_packet_t) && magic == MOONMIC_CTRL_START) {
                InterlockedExchange(&monitor->paused, 0);
            }
        }

        if (getTimeMs() - monitor->last_packet_time > ping_timeout_ms) {
            InterlockedExchange(&monitor->status, MOONMIC_DISCONNECTED);
            InterlockedExchange(&monitor->current_rtt, -1);
        }
        Sleep(10);
    }

    return 0;
}
} // namespace

extern "C" {

heartbeat_monitor_t* heartbeat_monitor_create(intptr_t socket_fd, const char* host_ip, uint16_t host_port) {
    if (socket_fd == static_cast<intptr_t>(INVALID_SOCKET) || !host_ip) {
        return nullptr;
    }

    auto* monitor = static_cast<heartbeat_monitor_t*>(calloc(1, sizeof(heartbeat_monitor_t)));
    if (!monitor) {
        return nullptr;
    }

    monitor->socket = static_cast<SOCKET>(socket_fd);
    monitor->destination.sin_family = AF_INET;
    monitor->destination.sin_port = htons(host_port);
    if (inet_pton(AF_INET, host_ip, &monitor->destination.sin_addr) != 1) {
        free(monitor);
        return nullptr;
    }

    monitor->last_packet_time = getTimeMs();
    InterlockedExchange(&monitor->status, MOONMIC_DISCONNECTED);
    InterlockedExchange(&monitor->current_rtt, -1);
    InterlockedExchange(&monitor->running, 1);

    monitor->thread_handle = CreateThread(nullptr, 0, monitorThread, monitor, 0, nullptr);
    if (!monitor->thread_handle) {
        free(monitor);
        return nullptr;
    }
    return monitor;
}

void heartbeat_monitor_destroy(heartbeat_monitor_t* monitor) {
    if (!monitor) {
        return;
    }

    InterlockedExchange(&monitor->running, 0);
    if (monitor->thread_handle) {
        WaitForSingleObject(monitor->thread_handle, INFINITE);
        CloseHandle(monitor->thread_handle);
    }
    free(monitor);
}

moonmic_connection_status_t heartbeat_monitor_get_status(heartbeat_monitor_t* monitor) {
    if (!monitor) {
        return MOONMIC_DISCONNECTED;
    }
    return static_cast<moonmic_connection_status_t>(InterlockedCompareExchange(&monitor->status, 0, 0));
}

int heartbeat_monitor_get_rtt(heartbeat_monitor_t* monitor) {
    return monitor ? InterlockedCompareExchange(&monitor->current_rtt, 0, 0) : -1;
}

bool heartbeat_monitor_is_connected(heartbeat_monitor_t* monitor) {
    return heartbeat_monitor_get_status(monitor) == MOONMIC_CONNECTED;
}

bool heartbeat_monitor_is_paused(heartbeat_monitor_t* monitor) {
    return monitor && InterlockedCompareExchange(&monitor->paused, 0, 0) != 0;
}
}
