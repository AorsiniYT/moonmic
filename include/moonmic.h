
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "heartbeat_monitor.h"
#include "moonmic_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MOONMIC_DEFAULT_SAMPLE_RATE 16000

#define MOONMIC_DEFAULT_CHANNELS 1

#define MOONMIC_DEFAULT_BITRATE 64000

#define MOONMIC_DEFAULT_PORT 48100

#define MOONMIC_DEFAULT_GAIN 1.0f

typedef struct moonmic_client_t moonmic_client_t;

typedef struct {
    const char* host_ip;
    uint16_t port;
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t bitrate;
    bool raw_mode;
    bool auto_start;
    float gain;

    const char* uniqueid;
    const char* devicename;
    int sunshine_https_port;
    const char* cert_path;
    const char* key_path;
    int pair_status;

    uint16_t target_display_width;
    uint16_t target_display_height;
} moonmic_config_t;

typedef void (*moonmic_error_callback_t)(const char* error, void* userdata);

typedef void (*moonmic_status_callback_t)(bool connected, void* userdata);

/**
 * @brief Create a new Moonmic client instance
 * @param config Configuration parameters (must not be NULL)
 * @return Pointer to client instance, or NULL on failure
 */
moonmic_client_t* moonmic_create(const moonmic_config_t* config);

void moonmic_destroy(moonmic_client_t* client);

bool moonmic_start(moonmic_client_t* client);

void moonmic_stop(moonmic_client_t* client);

bool moonmic_is_active(moonmic_client_t* client);

void moonmic_set_error_callback(moonmic_client_t* client, moonmic_error_callback_t callback, void* userdata);

void moonmic_set_gain(moonmic_client_t* client, float gain);

void moonmic_set_status_callback(moonmic_client_t* client, moonmic_status_callback_t callback, void* userdata);

moonmic_connection_status_t moonmic_get_connection_status(moonmic_client_t* client);

bool moonmic_is_connected(moonmic_client_t* client);

const char* moonmic_get_version();

int moonmic_client_get_rtt(moonmic_client_t* client);

#ifdef __cplusplus
}
#endif
