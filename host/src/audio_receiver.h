
#pragma once

#include "config.h"
#include "sunshine_integration.h"
#include "sunshine_webui.h"
#include "codec/ffmpeg_decoder.h"
#include "network/udp_receiver.h"
#include "network/connection_monitor.h"
#include "platform/virtual_device.h"
#include "display_manager.h"
#include <speex/speex_resampler.h>
#include <memory>
#include <string>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <mutex>

namespace moonmic {

#pragma pack(push, 1)
struct MoonmicHandshake {
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
};
#pragma pack(pop)

struct MoonmicPing;

// NOTE: SunshineWebUI removed - UUID verification not possible because

class AudioReceiver {
public:
    AudioReceiver();
    ~AudioReceiver();

    bool start(const Config& config);

    void stop();

    bool isRunning() const { return running_; }

    void pause();
    void resume();
    bool isPaused() const { return paused_; }

    bool switchAudioOutput(bool use_speakers);

    void setSunshineWebUI(SunshineWebUI* webui) { sunshine_webui_ = webui; }
    void setDisplayManager(DisplayManager* display_mgr) { display_manager_ = display_mgr; }

    struct Stats {
        uint64_t packets_received = 0;
        uint64_t packets_dropped = 0;
        uint64_t packets_dropped_lag = 0; // New: Auto-corrected drops due to lag
        uint64_t bytes_received = 0;
        std::string last_sender_ip;
        std::string client_name;
        bool is_connected = false;
        bool is_receiving = false;
        bool is_paused = false;
        int rtt_ms = -1;
    };

    Stats getStats();

private:
    void onPacketReceived(const uint8_t* data, size_t size, const std::string& sender_ip, uint16_t sender_port, bool is_lagging = false);
    bool isClientAllowed(const std::string& ip);
    bool validateHandshake(const uint8_t* data, size_t size, const std::string& sender_ip, uint16_t& out_w, uint16_t& out_h);
    void sendControlSignal(uint32_t signal_magic);
    bool applyDisplayResolution(uint16_t width, uint16_t height);
    bool applyFallbackDisplayResolution(uint16_t width, uint16_t height);
    void resetConnectionState();

    void pauseInternal();
    void resumeInternal();
    void sendControlSignalInternal(uint32_t signal_magic);

    Config config_;
    std::unique_ptr<SunshineIntegration> sunshine_;
    SunshineWebUI* sunshine_webui_ = nullptr;
    DisplayManager* display_manager_ = nullptr; // Optional direct display control fallback
    std::unique_ptr<FFmpegDecoder> decoder_;
    SpeexResamplerState* resampler_;
    std::unique_ptr<UDPReceiver> receiver_;
    std::unique_ptr<VirtualDevice> virtual_device_;
    std::unique_ptr<ConnectionMonitor> connection_monitor_;

    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    Stats stats_;

    bool client_validated_;
    std::string client_uniqueid_;
    std::string client_devicename_;
    std::string last_validated_ip_;

    uint32_t detected_stream_rate_ = 0;
    int system_sample_rate_ = 0;
    bool rate_logged_ = false;

    std::chrono::steady_clock::time_point last_packet_time_;
    std::chrono::steady_clock::time_point last_validated_time_;
    static constexpr int CONNECTION_TIMEOUT_MS = 2000;

    static constexpr size_t MAX_FRAMES = 5760;
    float decode_buffer_[MAX_FRAMES * 2];
    float resample_buffer_[MAX_FRAMES * 2];

    std::mutex audio_mutex_;
};

}
