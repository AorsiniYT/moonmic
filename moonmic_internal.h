/**
 * @file moonmic_internal.h
 * @brief Internal types and definitions for moonmic
 */

#pragma once

#include "moonmic.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // For size_t

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct moonmic_opus_encoder_t moonmic_opus_encoder_t;
typedef struct udp_sender_t udp_sender_t;
typedef struct audio_capture_t audio_capture_t;

// Magic constants
#define MOONMIC_HANDSHAKE_MAGIC     0x4D4F4F4E  // "MOON"
#define MOONMIC_HANDSHAKE_MAGIC_ALT 0x4E4F4F4D  // "NOOM"
#define MOONMIC_HANDSHAKE_ACK       0x4B434148  // "HACK"
#define MOONMIC_PONG_MAGIC          0x504F4E47  // "PONG"

// Handshake packet structure (matches host)
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;           // 0x4D4F4F4E ("MOON")
    uint8_t version;          // 2 (bumped for protocol extension)
    uint8_t pair_status;      // 0 or 1 from Sunshine validation
    uint8_t uniqueid_len;     // Length of uniqueid (16)
    char uniqueid[16];        // Client uniqueid
    uint8_t devicename_len;   // Length of devicename
    char devicename[64];      // Device name
    uint16_t display_width;   // Target display width (e.g., 1280, 1920)
    uint16_t display_height;  // Target display height (e.g., 720, 1080)
    uint8_t flags;            // Flags (e.g. FORCE_UPDATE)
} moonmic_handshake_t;

#define MOONMIC_FLAG_FORCE_UPDATE 0x01

#pragma pack(pop)

/**
 * @brief Internal client structure
 */
struct moonmic_client_t {
    // Configuration
    moonmic_config_t config;
    
    // Components
    audio_capture_t* capture;
    moonmic_opus_encoder_t* encoder;
    udp_sender_t* sender;
    
    // State
    bool active;
    bool running;
    
    // Frame accumulation buffer for Opus (Vita gives 256, Opus needs 320)
    float* accumulation_buffer;
    size_t accumulated_samples;  // Current samples in buffer
    size_t target_frame_size;    // Target frame size for Opus (320 @ 16kHz)
    
    // Callbacks
    moonmic_error_callback_t error_callback;
    void* error_userdata;
    moonmic_status_callback_t status_callback;
    void* status_userdata;
    
    // Threading (platform-specific)
    void* thread_handle;
    
    // Handshake tracking
    bool handshake_sent;
    
    // String storage (copies to prevent dangling pointers)
    char uniqueid_storage[32];
    char devicename_storage[128];
    
    // Heartbeat monitor
    struct heartbeat_monitor_t* heartbeat_monitor;
};

/**
 * @brief Audio capture interface (platform-specific)
 */
struct audio_capture_t {
    /**
     * @brief Initialize audio capture
     * @param sample_rate Sample rate in Hz
     * @param channels Number of channels (1 or 2)
     * @return true on success, false on failure
     */
    bool (*init)(audio_capture_t* self, uint32_t sample_rate, uint8_t channels);
    
    /**
     * @brief Get native sample rate supported by this platform
     * @return Native sample rate in Hz (e.g., 16000 for Vita)
     */
    uint32_t (*get_native_sample_rate)(audio_capture_t* self);
    
    /**
     * @brief Read audio samples
     * @param buffer Output buffer (float32 format)
     * @param frames Number of frames to read
     * @return Number of frames read, or -1 on error
     */
    int (*read)(audio_capture_t* self, float* buffer, size_t frames);
    
    /**
     * @brief Close audio capture
     */
    void (*close)(audio_capture_t* self);
    
    // Platform-specific data
    void* platform_data;
};

/**
 * @brief Opus encoder wrapper
 */
struct moonmic_opus_encoder_t {
    void* encoder;  // OpusEncoder*
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t bitrate;
};

/**
 * @brief Internal UDP sender structure
 */
struct udp_sender_t {
    int socket_fd;
    char host_ip[64];
    uint16_t port;
    uint32_t sequence;
};

/**
 * @brief Packet header for UDP transmission
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;      // 0x4D4D4943 ("MMIC")
    uint32_t sequence;   // Packet sequence number
    uint64_t timestamp;  // Microseconds since start
    uint32_t sample_rate; // Sample rate of encoded audio (e.g., 16000, 48000)
                         // Bit 31: RAW mode flag (1 = uncompressed PCM, 0 = Opus)
} moonmic_packet_header_t;

#define MOONMIC_MAGIC 0x4D4D4943
#define MOONMIC_RAW_FLAG 0x80000000  // Bit 31 set = RAW mode
#define MOONMIC_VERSION "1.0.0"
// Header size: magic(4) + sequence(4) + timestamp(8) + sample_rate(4) = 20 bytes
// Use this constant instead of sizeof() due to compiler alignment issues on ARM
#define MOONMIC_HEADER_SIZE 20

// Control signal magic numbers (host -> client)
#define MOONMIC_CTRL_STOP  0x53544F50  // "STOP" - host is pausing, stop transmitting
#define MOONMIC_CTRL_START 0x53545254  // "STRT" - host is resuming, start transmitting

// Control packet structure (8 bytes)
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;      // MOONMIC_CTRL_STOP or MOONMIC_CTRL_START
    uint32_t reserved;   // Reserved for future use
} moonmic_control_packet_t;
#pragma pack(pop)

// Receiver state enum
typedef enum {
    MOONMIC_STATE_STOPPED = 0,     // Not running
    MOONMIC_STATE_RUNNING = 1,     // Running and receiving
    MOONMIC_STATE_PAUSED = 2,      // Connected but not receiving (host paused)
    MOONMIC_STATE_SUSPENSION = 3   // Waiting for host (probing)
} moonmic_receiver_state_t;

// Platform-specific factory functions
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

// Codec functions (renamed to avoid conflicts with libopus)
moonmic_opus_encoder_t* moonmic_opus_encoder_create(uint32_t sample_rate, uint8_t channels, uint32_t bitrate);
void moonmic_opus_encoder_destroy(moonmic_opus_encoder_t* encoder);
int moonmic_opus_encoder_encode(moonmic_opus_encoder_t* encoder, const float* pcm, int frame_size, 
                       uint8_t* output, int max_output_bytes);

// Speex resampler functions
typedef struct moonmic_speex_resampler_t moonmic_speex_resampler_t;
moonmic_speex_resampler_t* moonmic_speex_resampler_create(uint32_t in_rate, uint32_t out_rate, uint8_t channels);
void moonmic_speex_resampler_destroy(moonmic_speex_resampler_t* resampler);
int moonmic_speex_resampler_process(moonmic_speex_resampler_t* resampler, 
                                     const int16_t* input, uint32_t in_frames,
                                     int16_t* output, uint32_t* out_frames);


// Network functions
udp_sender_t* udp_sender_create(const char* host_ip, uint16_t port);
void udp_sender_destroy(udp_sender_t* sender);
bool udp_sender_send(udp_sender_t* sender, const void* data, size_t size);

// Utility functions
uint64_t moonmic_get_timestamp_us(void);
void* moonmic_thread_create(void* (*func)(void*), void* arg);
void moonmic_thread_join(void* thread_handle);

#ifdef __cplusplus
}
#endif
