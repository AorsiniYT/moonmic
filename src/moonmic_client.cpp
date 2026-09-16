
#include "moonmic.h"
#include "moonmic_internal.h"
#include "moonmic_debug.h"
#include "heartbeat_monitor.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <memory>
#include <new>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include "psvita/platform_config.h"
#elif defined(_WIN32)
#include "windows/platform_config.h"
#else
#include "linux/platform_config.h"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

struct MoonmicThreadContext {
    void* (*function)(void*);
    void* argument;
};

static DWORD WINAPI moonmic_windows_thread(LPVOID argument) {
    auto* context = static_cast<MoonmicThreadContext*>(argument);
    auto function = context->function;
    void* function_argument = context->argument;
    delete context;
    function(function_argument);
    return 0;
}
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#endif

using HandshakeWire = std::array<uint8_t, MOONMIC_HANDSHAKE_SIZE>;

static void sleepMilliseconds(unsigned int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

static HandshakeWire buildHandshake(const moonmic_client_t* client) {
    moonmic_handshake_t handshake{};
    handshake.magic = MOONMIC_HANDSHAKE_MAGIC;
    handshake.version = MOONMIC_PROTOCOL_VERSION;
    handshake.pair_status = client->config.pair_status;
    handshake.display_width = client->config.target_display_width;
    handshake.display_height = client->config.target_display_height;

    if (client->config.uniqueid && client->config.uniqueid[0]) {
        handshake.uniqueid_len = static_cast<uint8_t>(
            std::min(strlen(client->config.uniqueid), static_cast<size_t>(MOONMIC_UNIQUE_ID_CAPACITY)));
        memcpy(handshake.uniqueid, client->config.uniqueid, handshake.uniqueid_len);
    }
    if (client->config.devicename && client->config.devicename[0]) {
        handshake.devicename_len = static_cast<uint8_t>(
            std::min(strlen(client->config.devicename), static_cast<size_t>(MOONMIC_DEVICE_NAME_CAPACITY)));
        memcpy(handshake.devicename, client->config.devicename, handshake.devicename_len);
    }

    HandshakeWire wire{};
    moonmic_write_handshake_le(wire.data(), &handshake);
    return wire;
}

static bool sendHandshake(moonmic_client_t* client, const HandshakeWire& handshake, bool announce) {
    const bool sent = udp_sender_send(client->sender, handshake.data(), handshake.size());
    if (announce) {
        if (sent) {
            MOONMIC_LOG("[moonmic_worker] Handshake sent: device='%s', resolution=%dx%d",
                        client->config.devicename ? client->config.devicename : "unknown",
                        client->config.target_display_width, client->config.target_display_height);
        } else {
            MOONMIC_LOG("[moonmic_worker] WARNING: Failed to send handshake");
        }
    }
    return sent;
}

struct HostConnectionState {
    bool was_connected = true;
    uint64_t last_probe_time = 0;
    int probe_count = 0;
};

static bool hostReadyForAudio(moonmic_client_t* client, HostConnectionState& state, const HandshakeWire& handshake) {
    if (!client->heartbeat_monitor) return true;

    const bool connected = heartbeat_monitor_is_connected(client->heartbeat_monitor);
    if (!connected && state.was_connected) {
        MOONMIC_LOG("[moonmic_worker] Host disconnected - probing every 3 seconds");
        state.was_connected = false;
        state.last_probe_time = 0;
        state.probe_count = 0;
    }

    if (!connected) {
        const uint64_t now = moonmic_get_timestamp_us() / 1000;
        if (now - state.last_probe_time >= 3000) {
            ++state.probe_count;
            sendHandshake(client, handshake, false);
            MOONMIC_LOG("[moonmic_worker] Probe #%d: waiting for host...", state.probe_count);
            state.last_probe_time = now;
        }
        sleepMilliseconds(200);
        return false;
    }

    if (!state.was_connected) {
        MOONMIC_LOG("[moonmic_worker] Host is back online - resuming transmission");
        sendHandshake(client, handshake, true);
        state.was_connected = true;
        state.probe_count = 0;
    }

    if (heartbeat_monitor_is_paused(client->heartbeat_monitor)) {
        sleepMilliseconds(100);
        return false;
    }
    return true;
}

static void applyGain(float* samples, size_t sample_count, float gain) {
    for (size_t index = 0; index < sample_count; ++index) {
        samples[index] = std::clamp(samples[index] * gain, -1.0f, 1.0f);
    }
}

static void sendAudioPacket(moonmic_client_t* client, uint8_t* packet, size_t payload_size, uint32_t sample_rate) {
    moonmic_write_packet_header(packet, udp_sender_next_sequence(client->sender), moonmic_get_timestamp_us(),
                                sample_rate);
    udp_sender_send(client->sender, packet, MOONMIC_HEADER_SIZE + payload_size);
}

static void* moonmic_worker_thread(void* arg) {
    auto* client = static_cast<moonmic_client_t*>(arg);
    const HandshakeWire handshake = buildHandshake(client);
    sendHandshake(client, handshake, true);

    const int frame_size = PLATFORM_GRAIN_SIZE;
    const size_t buffer_size = static_cast<size_t>(frame_size) * client->config.channels;
    auto pcm_buffer = std::unique_ptr<float[]>(new (std::nothrow) float[buffer_size]);
    auto opus_buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[PLATFORM_OPUS_BUFFER_SIZE]);

    if (!pcm_buffer || !opus_buffer) {
        if (client->error_callback) {
            client->error_callback("Failed to allocate buffers", client->error_userdata);
        }
        return NULL;
    }

    MOONMIC_LOG("[moonmic_worker] Thread started - beginning capture loop");
    HostConnectionState connection;

    MOONMIC_LOG("[moonmic_worker] Flushing audio buffer...");
    for (int i = 0; i < 10; i++) {
        int read = client->capture->read(client->capture, pcm_buffer.get(), frame_size);
        if (read <= 0) break;
    }
    MOONMIC_LOG("[moonmic_worker] Flush complete.");

    while (client->running) {

        if (!hostReadyForAudio(client, connection, handshake)) continue;

        int frames_read = client->capture->read(client->capture, pcm_buffer.get(), frame_size);

        if (frames_read < 0) {
            if (client->error_callback) {
                client->error_callback("Audio capture failed", client->error_userdata);
            }
            break;
        }

        if (frames_read == 0) {

            sleepMilliseconds(1);
            continue;
        }

        if (client->config.raw_mode) {

            const size_t sample_count = static_cast<size_t>(frames_read) * client->config.channels;
            applyGain(pcm_buffer.get(), sample_count, client->config.gain);
            uint8_t* pcm_output = opus_buffer.get() + MOONMIC_HEADER_SIZE;
            for (size_t i = 0; i < sample_count; i++) {
                int16_t sample = static_cast<int16_t>(pcm_buffer[i] * 32767.0f);
                moonmic_write_u16_le(pcm_output + (i * sizeof(sample)), static_cast<uint16_t>(sample));
            }
            size_t encoded_bytes = sample_count * sizeof(int16_t);
            sendAudioPacket(client, opus_buffer.get(), encoded_bytes, client->config.sample_rate | MOONMIC_RAW_FLAG);
            continue;
        }

        const size_t samples_to_copy = static_cast<size_t>(frames_read) * client->config.channels;
        const size_t space_available =
            (client->target_frame_size - client->accumulated_samples) * client->config.channels;
        applyGain(pcm_buffer.get(), samples_to_copy, client->config.gain);

        if (samples_to_copy <= space_available) {

            memcpy(client->accumulation_buffer.get() + client->accumulated_samples * client->config.channels,
                   pcm_buffer.get(), samples_to_copy * sizeof(float));
            client->accumulated_samples += frames_read;
        } else {
            const size_t samples_fitting = space_available;
            memcpy(client->accumulation_buffer.get() + client->accumulated_samples * client->config.channels,
                   pcm_buffer.get(), samples_fitting * sizeof(float));
            client->accumulated_samples += samples_fitting / client->config.channels;
        }

        size_t samples_leftover = 0;
        size_t leftover_offset = 0;
        if (samples_to_copy > space_available) {
            samples_leftover = samples_to_copy - space_available;
            leftover_offset = space_available;
        }

        if (client->accumulated_samples >= client->target_frame_size) {
            int encoded_bytes = moonmic_opus_encoder_encode(
                client->encoder, client->accumulation_buffer.get(), static_cast<int>(client->target_frame_size),
                opus_buffer.get() + MOONMIC_HEADER_SIZE, PLATFORM_OPUS_BUFFER_SIZE - MOONMIC_HEADER_SIZE);

            if (encoded_bytes < 0) {
                if (client->error_callback) {
                    client->error_callback("Opus encoding failed", client->error_userdata);
                }
                client->accumulated_samples = 0;
                continue;
            }

            sendAudioPacket(client, opus_buffer.get(), static_cast<size_t>(encoded_bytes), client->config.sample_rate);

            client->accumulated_samples = 0;

            if (samples_leftover > 0) {
                memcpy(client->accumulation_buffer.get(), pcm_buffer.get() + leftover_offset,
                       samples_leftover * sizeof(float));
                client->accumulated_samples = samples_leftover / client->config.channels;
            }
        }
    }

    return NULL;
}

moonmic_client_t* moonmic_create(const moonmic_config_t* config) {
    if (!config || !config->host_ip) {
        MOONMIC_LOG("[moonmic_create] ERROR: Invalid config or host_ip is NULL");
        return NULL;
    }

    MOONMIC_LOG("[moonmic_create] Creating client for %s:%d", config->host_ip, config->port);

    auto* client = new (std::nothrow) moonmic_client_t();
    if (!client) {
        MOONMIC_LOG("[moonmic_create] ERROR: Failed to allocate client memory");
        return NULL;
    }

    client->config = *config;

    if (config->uniqueid && config->uniqueid[0]) {
        snprintf(client->uniqueid_storage, sizeof(client->uniqueid_storage), "%s", config->uniqueid);
        client->config.uniqueid = client->uniqueid_storage;
    } else {
        client->uniqueid_storage[0] = '\0';
        client->config.uniqueid = NULL;
    }

    if (config->devicename && config->devicename[0]) {
        snprintf(client->devicename_storage, sizeof(client->devicename_storage), "%s", config->devicename);
        client->config.devicename = client->devicename_storage;
    } else {
        client->devicename_storage[0] = '\0';
        client->config.devicename = NULL;
    }

    MOONMIC_LOG("[moonmic_create] Copied strings: uniqueid='%s', devicename='%s'",
                client->config.uniqueid ? client->config.uniqueid : "(null)",
                client->config.devicename ? client->config.devicename : "(null)");

    if (client->config.port == 0) {
        client->config.port = MOONMIC_DEFAULT_PORT;
    }
    if (client->config.sample_rate == 0) {
        client->config.sample_rate = PLATFORM_SAMPLE_RATE;
    }
    if (client->config.channels == 0) {
        client->config.channels = PLATFORM_CHANNELS;
    }
    if (client->config.bitrate == 0) {
        client->config.bitrate = PLATFORM_BITRATE;
    }
    if (client->config.gain <= 0.0f) {
        client->config.gain = MOONMIC_DEFAULT_GAIN;
    } else {
        client->config.gain = std::clamp(client->config.gain, 1.0f, 100.0f);
    }

    MOONMIC_LOG("[moonmic_create] Config: %dHz, %dch, %dbps, port=%d", client->config.sample_rate,
                client->config.channels, client->config.bitrate, client->config.port);

#ifdef __vita__
    MOONMIC_LOG("[moonmic_create] Creating Vita audio capture");
    client->capture = audio_capture_create_vita();
#elif _WIN32
    client->capture = audio_capture_create_windows();
#elif __linux__
    client->capture = audio_capture_create_linux();
#elif __APPLE__
    client->capture = audio_capture_create_macos();
#elif __ANDROID__
    client->capture = audio_capture_create_android();
#else
#error "Unsupported platform"
#endif

    if (!client->capture) {
        MOONMIC_LOG("[moonmic_create] ERROR: Failed to create audio capture");
        delete client;
        return NULL;
    }

    MOONMIC_LOG("[moonmic_create] Initializing audio capture");

    if (!client->capture->init(client->capture, client->config.sample_rate, client->config.channels)) {
        MOONMIC_LOG("[moonmic_create] ERROR: Failed to initialize audio capture");
        client->capture->close(client->capture);
        free(client->capture);
        delete client;
        return NULL;
    }

    const uint32_t encoder_sample_rate = client->capture->get_native_sample_rate(client->capture);
    if (encoder_sample_rate == 0) {
        client->capture->close(client->capture);
        free(client->capture);
        delete client;
        return NULL;
    }
    client->config.sample_rate = encoder_sample_rate;
    client->target_frame_size = encoder_sample_rate / 50;

    if (client->config.raw_mode) {
        MOONMIC_LOG("[moonmic_create] RAW mode enabled - skipping Opus encoder");
        client->encoder = NULL;
    } else {
        MOONMIC_LOG("[moonmic_create] Creating Opus encoder");

        uint32_t encoder_bitrate = client->config.bitrate;

        MOONMIC_LOG("[moonmic_create] Using %uHz for Opus (platform native rate)", encoder_sample_rate);

        client->encoder = moonmic_opus_encoder_create(encoder_sample_rate, client->config.channels, encoder_bitrate);
        if (!client->encoder) {
            MOONMIC_LOG("[moonmic_create] ERROR: Failed to create Opus encoder");
            client->capture->close(client->capture);
            free(client->capture);
            delete client;
            return NULL;
        }
    }

    MOONMIC_LOG("[moonmic_create] Creating UDP sender to %s:%d", client->config.host_ip, client->config.port);

    client->sender = udp_sender_create(client->config.host_ip, client->config.port);
    if (!client->sender) {
        MOONMIC_LOG("[moonmic_create] ERROR: Failed to create UDP sender");
        moonmic_destroy(client);
        return NULL;
    }

    // Heartbeats share the sender socket so replies return to the bound source port.
    const intptr_t sender_socket = udp_sender_socket(client->sender);
    client->heartbeat_monitor = heartbeat_monitor_create(sender_socket, client->config.host_ip, client->config.port);

    if (client->heartbeat_monitor) {
        MOONMIC_LOG("[moonmic_create] Heartbeat monitor started on shared socket %lld", (long long)sender_socket);
    } else {
        MOONMIC_LOG("[moonmic_create] Failed to create heartbeat monitor");
        moonmic_destroy(client);
        return NULL;
    }

    if (!client->config.raw_mode) {
        size_t buffer_size = client->target_frame_size * client->config.channels;
        client->accumulation_buffer.reset(new (std::nothrow) float[buffer_size]());
        if (!client->accumulation_buffer) {
            MOONMIC_LOG("[moonmic_create] ERROR: Failed to allocate accumulation buffer");
            moonmic_destroy(client);
            return NULL;
        }
        MOONMIC_LOG("[moonmic_create] Allocated accumulation buffer: %zu samples", buffer_size);
    }

    MOONMIC_LOG("[moonmic_create] Client created successfully");

    if (client->config.auto_start) {
        MOONMIC_LOG("[moonmic_create] Auto-starting client");
        if (!moonmic_start(client)) {
            moonmic_destroy(client);
            return NULL;
        }
    }

    return client;
}

void moonmic_destroy(moonmic_client_t* client) {
    if (!client) {
        return;
    }

    moonmic_stop(client);

    if (client->heartbeat_monitor) {
        heartbeat_monitor_destroy(client->heartbeat_monitor);
    }
    if (client->sender) {
        udp_sender_destroy(client->sender);
    }
    if (client->encoder) {
        moonmic_opus_encoder_destroy(client->encoder);
    }
    if (client->capture) {
        client->capture->close(client->capture);
        free(client->capture);
    }

    delete client;
    MOONMIC_LOG("[moonmic_destroy] Client destroyed");
}

bool moonmic_start(moonmic_client_t* client) {
    if (!client || client->active) {
        return false;
    }

    client->running = true;
    client->active = true;

    client->thread_handle = moonmic_thread_create(moonmic_worker_thread, client);
    if (!client->thread_handle) {
        client->running = false;
        client->active = false;
        return false;
    }

    if (client->status_callback) {
        client->status_callback(true, client->status_userdata);
    }

    return true;
}

void moonmic_stop(moonmic_client_t* client) {
    if (!client || !client->active) {
        return;
    }

    client->running = false;

    if (client->thread_handle) {
        moonmic_thread_join(client->thread_handle);
        client->thread_handle = NULL;
    }

    client->active = false;

    if (client->status_callback) {
        client->status_callback(false, client->status_userdata);
    }
}

bool moonmic_is_active(moonmic_client_t* client) {
    return client && client->active;
}

void moonmic_set_error_callback(moonmic_client_t* client, moonmic_error_callback_t callback, void* userdata) {
    if (client) {
        client->error_callback = callback;
        client->error_userdata = userdata;
    }
}

void moonmic_set_status_callback(moonmic_client_t* client, moonmic_status_callback_t callback, void* userdata) {
    if (client) {
        client->status_callback = callback;
        client->status_userdata = userdata;
    }
}

const char* moonmic_get_version(void) {
    return MOONMIC_VERSION;
}

uint64_t moonmic_get_timestamp_us(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000) / frequency.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

void* moonmic_thread_create(void* (*func)(void*), void* arg) {
#ifdef _WIN32
    auto* context = new (std::nothrow) MoonmicThreadContext();
    if (!context) {
        return NULL;
    }

    context->function = func;
    context->argument = arg;
    HANDLE thread = CreateThread(NULL, 0, moonmic_windows_thread, context, 0, NULL);
    if (!thread) {
        delete context;
    }
    return (void*)thread;
#else
    auto* thread = new (std::nothrow) pthread_t;
    if (!thread) {
        return NULL;
    }
    if (pthread_create(thread, NULL, func, arg) != 0) {
        delete thread;
        return NULL;
    }
    return thread;
#endif
}

void moonmic_thread_join(void* thread_handle) {
    if (!thread_handle) {
        return;
    }
#ifdef _WIN32
    WaitForSingleObject((HANDLE)thread_handle, INFINITE);
    CloseHandle((HANDLE)thread_handle);
#else
    auto* thread = static_cast<pthread_t*>(thread_handle);
    pthread_join(*thread, NULL);
    delete thread;
#endif
}

void moonmic_set_gain(moonmic_client_t* client, float gain) {
    if (client) {
        client->config.gain = std::clamp(gain, 1.0f, 100.0f);
    }
}

moonmic_connection_status_t moonmic_get_connection_status(moonmic_client_t* client) {
    if (!client || !client->heartbeat_monitor) {
        return MOONMIC_DISCONNECTED;
    }
    return heartbeat_monitor_get_status(client->heartbeat_monitor);
}

bool moonmic_is_paused(moonmic_client_t* client) {
    if (!client || !client->heartbeat_monitor) return false;
    return heartbeat_monitor_is_paused(client->heartbeat_monitor);
}

int moonmic_client_get_rtt(moonmic_client_t* client) {
    if (!client || !client->heartbeat_monitor) return -1;
    return heartbeat_monitor_get_rtt(client->heartbeat_monitor);
}

bool moonmic_is_connected(moonmic_client_t* client) {
    return moonmic_get_connection_status(client) == MOONMIC_CONNECTED;
}
