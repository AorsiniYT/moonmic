
#pragma once

#include "../include/moonmic.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <atomic>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct moonmic_opus_encoder_t moonmic_opus_encoder_t;
typedef struct udp_sender_t udp_sender_t;
typedef struct audio_capture_t audio_capture_t;

struct moonmic_client_t {

    moonmic_config_t config{};

    audio_capture_t* capture = nullptr;
    moonmic_opus_encoder_t* encoder = nullptr;
    udp_sender_t* sender = nullptr;

    bool active = false;
    std::atomic<bool> running{false};

    std::unique_ptr<float[]> accumulation_buffer;
    size_t accumulated_samples = 0;
    size_t target_frame_size = 0;

    moonmic_error_callback_t error_callback = nullptr;
    void* error_userdata = nullptr;
    moonmic_status_callback_t status_callback = nullptr;
    void* status_userdata = nullptr;

    void* thread_handle = nullptr;

    char uniqueid_storage[32]{};
    char devicename_storage[128]{};

    struct heartbeat_monitor_t* heartbeat_monitor = nullptr;
};

struct audio_capture_t {

    bool (*init)(audio_capture_t* self, uint32_t sample_rate, uint8_t channels);

    uint32_t (*get_native_sample_rate)(audio_capture_t* self);

    int (*read)(audio_capture_t* self, float* buffer, size_t frames);

    void (*close)(audio_capture_t* self);

    void* platform_data;
};

struct moonmic_opus_encoder_t {
    void* encoder;
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t bitrate;
};

#ifdef __vita__
audio_capture_t* audio_capture_create_vita(void);
#elif _WIN32
audio_capture_t* audio_capture_create_windows(void);
#elif __linux__
audio_capture_t* audio_capture_create_linux(void);
#elif __APPLE__
audio_capture_t* audio_capture_create_macos(void);
#elif __ANDROID__
audio_capture_t* audio_capture_create_android(void);
#endif

moonmic_opus_encoder_t* moonmic_opus_encoder_create(uint32_t sample_rate, uint8_t channels, uint32_t bitrate);
void moonmic_opus_encoder_destroy(moonmic_opus_encoder_t* encoder);
int moonmic_opus_encoder_encode(moonmic_opus_encoder_t* encoder, const float* pcm, int frame_size, uint8_t* output,
                                int max_output_bytes);

udp_sender_t* udp_sender_create(const char* host_ip, uint16_t port);
void udp_sender_destroy(udp_sender_t* sender);
bool udp_sender_send(udp_sender_t* sender, const void* data, size_t size);
uint32_t udp_sender_next_sequence(udp_sender_t* sender);
intptr_t udp_sender_socket(const udp_sender_t* sender);

uint64_t moonmic_get_timestamp_us(void);
void* moonmic_thread_create(void* (*func)(void*), void* arg);
void moonmic_thread_join(void* thread_handle);

#ifdef __cplusplus
}
#endif
