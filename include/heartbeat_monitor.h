
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MOONMIC_DISCONNECTED = 0, MOONMIC_CONNECTED = 1 } moonmic_connection_status_t;

typedef struct heartbeat_monitor_t heartbeat_monitor_t;

heartbeat_monitor_t* heartbeat_monitor_create(intptr_t socket_fd, const char* host_ip, uint16_t host_port);

int heartbeat_monitor_get_rtt(heartbeat_monitor_t* monitor);

void heartbeat_monitor_destroy(heartbeat_monitor_t* monitor);

moonmic_connection_status_t heartbeat_monitor_get_status(heartbeat_monitor_t* monitor);

bool heartbeat_monitor_is_connected(heartbeat_monitor_t* monitor);

bool heartbeat_monitor_is_paused(heartbeat_monitor_t* monitor);

#ifdef __cplusplus
}
#endif
