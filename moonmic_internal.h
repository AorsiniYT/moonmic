
#pragma once

#include "moonmic.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct moonmic_opus_encoder_t moonmic_opus_encoder_t;
typedef struct udp_sender_t udp_sender_t;
typedef struct audio_capture_t audio_capture_t;

#define MOONMIC_HANDSHAKE_MAGIC     0x4D4F4F4E
#define MOONMIC_HANDSHAKE_MAGIC_ALT 0x4E4F4F4D
#define MOONMIC_HANDSHAKE_ACK       0x4B434148
#define MOONMIC_PONG_MAGIC          0x504F4E47

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t pair_status;
    uint8_t uniqueid_len;
    char uniqueid[16];
    uint8_t devicename_len;
    char devicename[64];
    uint16_t display_width;
    uint16_t display_height;
    uint8_t flags;
} moonmic_handshake_t;

#define MOONMIC_FLAG_FORCE_UPDATE 0x01

#pragma pack(pop)

struct moonmic_client_t {

    moonmic_config_t config;

    audio_capture_t* capture;
    moonmic_opus_encoder_t* encoder;
    udp_sender_t* sender;

    bool active;
    bool running;

    float* accumulation_buffer;
    size_t accumulated_samples;
    size_t target_frame_size;

    moonmic_error_callback_t error_callback;
    void* error_userdata;
    moonmic_status_callback_t status_callback;
    void* status_userdata;

    void* thread_handle;

    bool handshake_sent;

    char uniqueid_storage[32];
    char devicename_storage[128];

    struct heartbeat_monitor_t* heartbeat_monitor;
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

struct udp_sender_t {
    int socket_fd;
    char host_ip[64];
    uint16_t port;
    uint32_t sequence;
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t sequence;
    uint64_t timestamp;
    uint32_t sample_rate;

} moonmic_packet_header_t;

#define MOONMIC_MAGIC 0x4D4D4943
#define MOONMIC_RAW_FLAG 0x80000000
#define MOONMIC_VERSION "1.0.0"

// Use this constant instead of sizeof() due to compiler alignment issues on ARM
#define MOONMIC_HEADER_SIZE 20

#define MOONMIC_CTRL_STOP  0x53544F50
#define MOONMIC_CTRL_START 0x53545254

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t reserved;
} moonmic_control_packet_t;
#pragma pack(pop)

typedef enum {
    MOONMIC_STATE_STOPPED = 0,
    MOONMIC_STATE_RUNNING = 1,
    MOONMIC_STATE_PAUSED = 2,
    MOONMIC_STATE_SUSPENSION = 3
} moonmic_receiver_state_t;

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
int moonmic_opus_encoder_encode(moonmic_opus_encoder_t* encoder, const float* pcm, int frame_size,
                       uint8_t* output, int max_output_bytes);

typedef struct moonmic_speex_resampler_t moonmic_speex_resampler_t;
moonmic_speex_resampler_t* moonmic_speex_resampler_create(uint32_t in_rate, uint32_t out_rate, uint8_t channels);
void moonmic_speex_resampler_destroy(moonmic_speex_resampler_t* resampler);
int moonmic_speex_resampler_process(moonmic_speex_resampler_t* resampler,
                                     const int16_t* input, uint32_t in_frames,
                                     int16_t* output, uint32_t* out_frames);

udp_sender_t* udp_sender_create(const char* host_ip, uint16_t port);
void udp_sender_destroy(udp_sender_t* sender);
bool udp_sender_send(udp_sender_t* sender, const void* data, size_t size);

uint64_t moonmic_get_timestamp_us(void);
void* moonmic_thread_create(void* (*func)(void*), void* arg);
void moonmic_thread_join(void* thread_handle);

#ifdef __cplusplus
}
#endif
