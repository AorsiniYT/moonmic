
#include "heartbeat_monitor.h"
#include "moonmic_protocol.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <poll.h>

#define PING_TIMEOUT_MS 3000

struct heartbeat_monitor_t {
    int socket;
    struct sockaddr_in dest_addr;
    volatile int running;
    volatile moonmic_connection_status_t status;
    volatile uint64_t last_ping_time;
    volatile int current_rtt;
    SceUID thread_id;
    volatile int paused;
};

static uint64_t get_time_ms() {
    return sceKernelGetProcessTimeLow() / 1000;
}

static int monitor_thread_func(SceSize args, void* argp) {

    if (!argp) {
        printf("[heartbeat_mon] Error: argp is NULL\n");
        return sceKernelExitDeleteThread(1);
    }

    heartbeat_monitor_t* monitor = *(heartbeat_monitor_t**)argp;

    if (!monitor) {
        printf("[heartbeat_mon] Error: monitor is NULL\n");
        return sceKernelExitDeleteThread(1);
    }

    printf("[heartbeat_mon] Thread started. Socket: %d, Target: %s:%d\n", monitor->socket,
           inet_ntoa(monitor->dest_addr.sin_addr), ntohs(monitor->dest_addr.sin_port));

    uint8_t buffer[32];

    struct pollfd pfd;
    pfd.fd = monitor->socket;
    pfd.events = POLLIN;

    uint64_t last_sent_ping = 0;

    while (monitor->running) {

        uint64_t now = get_time_ms();
        if (now - last_sent_ping >= 1000) {
            uint8_t packet[sizeof(moonmic_ping_packet_t)];
            moonmic_write_ping_le(packet, MOONMIC_PING_MAGIC, now);

            sendto(monitor->socket, packet, sizeof(packet), 0, (struct sockaddr*)&monitor->dest_addr,
                   sizeof(monitor->dest_addr));

            last_sent_ping = now;
        }

        int poll_ret = poll(&pfd, 1, 100);

        if (poll_ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t received = recv(monitor->socket, buffer, sizeof(buffer), 0);

            if (received >= 4) {
                const uint32_t magic = moonmic_read_u32_le(buffer);

                if (magic == MOONMIC_PING_MAGIC && received == sizeof(moonmic_ping_packet_t)) {

                    monitor->last_ping_time = get_time_ms();
                    monitor->status = MOONMIC_CONNECTED;

                    uint8_t pong[sizeof(moonmic_ping_packet_t)];
                    moonmic_write_ping_le(pong, MOONMIC_PONG_MAGIC, moonmic_read_ping_timestamp_le(buffer));

                    sendto(monitor->socket, pong, sizeof(pong), 0, (struct sockaddr*)&monitor->dest_addr,
                           sizeof(monitor->dest_addr));
                } else if (magic == MOONMIC_PONG_MAGIC && received == sizeof(moonmic_ping_packet_t)) {

                    monitor->last_ping_time = get_time_ms();
                    monitor->status = MOONMIC_CONNECTED;

                    uint64_t ts = moonmic_read_ping_timestamp_le(buffer);
                    uint64_t current_time = get_time_ms();

                    int64_t diff = (int64_t)(current_time - ts);

                    if (diff >= 0 && diff < 5000) {
                        monitor->current_rtt = (int)diff;
                    }
                } else if (magic == MOONMIC_CTRL_STOP) {
                    monitor->paused = 1;
                    printf("[heartbeat_mon] Paused\n");
                } else if (magic == MOONMIC_CTRL_START) {
                    monitor->paused = 0;
                    printf("[heartbeat_mon] Resumed\n");
                }
            }
        }

        if (get_time_ms() - monitor->last_ping_time > PING_TIMEOUT_MS) {
            monitor->status = MOONMIC_DISCONNECTED;
            monitor->current_rtt = -1;
        }
    }

    printf("[heartbeat_mon] Thread exiting\n");
    return sceKernelExitDeleteThread(0);
}

extern "C" {

heartbeat_monitor_t* heartbeat_monitor_create(intptr_t socket_fd, const char* host_ip, uint16_t host_port) {
    heartbeat_monitor_t* monitor = (heartbeat_monitor_t*)calloc(1, sizeof(heartbeat_monitor_t));
    if (!monitor) {
        return nullptr;
    }

    monitor->socket = (int)socket_fd;

    if (monitor->socket < 0) {
        free(monitor);
        return nullptr;
    }

    memset(&monitor->dest_addr, 0, sizeof(monitor->dest_addr));
    monitor->dest_addr.sin_family = AF_INET;
    monitor->dest_addr.sin_port = htons(host_port);
    inet_pton(AF_INET, host_ip, &monitor->dest_addr.sin_addr);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(monitor->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    monitor->status = MOONMIC_DISCONNECTED;
    monitor->current_rtt = -1;
    monitor->running = 1;
    monitor->paused = 0;

    monitor->thread_id = sceKernelCreateThread("heartbeat_mon", monitor_thread_func, 0x10000100, 0x4000, 0, 0, nullptr);
    if (monitor->thread_id < 0) {
        free(monitor);
        return nullptr;
    }

    if (sceKernelStartThread(monitor->thread_id, sizeof(heartbeat_monitor_t*), &monitor) < 0) {
        sceKernelDeleteThread(monitor->thread_id);
        free(monitor);
        return nullptr;
    }

    return monitor;
}

void heartbeat_monitor_destroy(heartbeat_monitor_t* monitor) {
    if (!monitor) {
        return;
    }

    monitor->running = 0;

    if (monitor->thread_id >= 0) {
        sceKernelWaitThreadEnd(monitor->thread_id, nullptr, nullptr);
    }

    // Do NOT close shared socket here, udp_sender owns it

    free(monitor);
}

moonmic_connection_status_t heartbeat_monitor_get_status(heartbeat_monitor_t* monitor) {
    return monitor ? monitor->status : MOONMIC_DISCONNECTED;
}

int heartbeat_monitor_get_rtt(heartbeat_monitor_t* monitor) {
    return monitor ? monitor->current_rtt : -1;
}

bool heartbeat_monitor_is_connected(heartbeat_monitor_t* monitor) {
    return heartbeat_monitor_get_status(monitor) == MOONMIC_CONNECTED;
}

bool heartbeat_monitor_is_paused(heartbeat_monitor_t* monitor) {
    return monitor ? (monitor->paused != 0) : false;
}
}
