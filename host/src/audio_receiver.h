/**
 * @file audio_receiver.h
 * @brief Main audio receiver coordinator
 */

#pragma once

#include "config.h"
#include "sunshine_integration.h"
#include "sunshine_webui.h"  // Added for setDisplayResolution
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

// Handshake packet structure (matches moonmic client)
#pragma pack(push, 1)
struct MoonMicHandshake {
    uint32_t magic;           // 0x4D4F4F4E ("MOON")
    uint8_t version;          // 2 (bumped for protocol extension)
    uint8_t pair_status;      // 0 or 1 from Sunshine validation
    uint8_t uniqueid_len;     // Length of uniqueid (16)
    char uniqueid[16];        // Client uniqueid
    uint8_t devicename_len;   // Length of devicename
    char devicename[64];      // Device name
    uint16_t display_width;   // Target display width (0 = don't configure)
    uint16_t display_height;  // Target display height (0 = don't configure)
    uint8_t flags;            // Flags (0x01 = FORCE_UPDATE)
};
#pragma pack(pop)

// Forward declaration (defined in connection_monitor.h)
struct MoonMicPing;

// NOTE: SunshineWebUI removed - UUID verification not possible because
// Sunshine generates random UUID during pairing. Using pair_status instead.

/**
 * @brief Audio receiver with Opus/PCM support
 */
class AudioReceiver {
public:
    AudioReceiver();
    ~AudioReceiver();
    
    /**
     * @brief Start the audio receiver
     * Initializes network, decoder, and virtual audio device based on config
     * @param config Application configuration
     * @return true if started successfully
     */
    bool start(const Config& config);

    /**
     * @brief Stop the audio receiver
     * Cleanly shuts down network and restores original audio device
     */
    void stop();

    /**
     * @brief Check if receiver main loop is running
     */
    bool isRunning() const { return running_; }
    
    // Pause/Resume - maintains connection but stops/resumes audio processing
    void pause();   // Sends STOP signal to client
    void resume();  // Sends START signal to client
    bool isPaused() const { return paused_; }
    
    // Hot-swap audio output without restarting connection
    bool switchAudioOutput(bool use_speakers);
    
    // Set Sunshine WebUI instance for resolution control
    void setSunshineWebUI(SunshineWebUI* webui) { sunshine_webui_ = webui; }
    void setDisplayManager(DisplayManager* display_mgr) { display_manager_ = display_mgr; }
    
    // Stats
    struct Stats {
        uint64_t packets_received = 0;
        uint64_t packets_dropped = 0;
        uint64_t packets_dropped_lag = 0; // New: Auto-corrected drops due to lag
        uint64_t bytes_received = 0;
        std::string last_sender_ip;
        std::string client_name; 
        bool is_connected = false;   // Heartbeat alive (client validated)
        bool is_receiving = false;   // Actually receiving audio data
        bool is_paused = false;      // Receiver is paused
        int rtt_ms = -1;             // Round trip time (ms)
    };
    
    Stats getStats();  // Checks for connection timeout
    
private:
    void onPacketReceived(const uint8_t* data, size_t size, const std::string& sender_ip, uint16_t sender_port, bool is_lagging = false);
    bool isClientAllowed(const std::string& ip);
    bool validateHandshake(const uint8_t* data, size_t size, const std::string& sender_ip, uint16_t& out_w, uint16_t& out_h);
    void sendControlSignal(uint32_t signal_magic);  // Send STOP/START to client
    bool applyDisplayResolution(uint16_t width, uint16_t height);
    bool applyFallbackDisplayResolution(uint16_t width, uint16_t height);
    void resetConnectionState();
    
    // Internal helpers that assume mutex is already locked
    void pauseInternal();
    void resumeInternal();
    void sendControlSignalInternal(uint32_t signal_magic);

    Config config_;
    std::unique_ptr<SunshineIntegration> sunshine_;
    SunshineWebUI* sunshine_webui_ = nullptr;  // Pointer to WebUI instance
    DisplayManager* display_manager_ = nullptr; // Optional direct display control fallback
    std::unique_ptr<FFmpegDecoder> decoder_;
    SpeexResamplerState* resampler_;  // Speex resampler (16kHz -> 48kHz)
    std::unique_ptr<UDPReceiver> receiver_;
    std::unique_ptr<VirtualDevice> virtual_device_;
    std::unique_ptr<ConnectionMonitor> connection_monitor_;
    
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    Stats stats_;
    
    // Handshake validation state
    bool client_validated_;
    std::string client_uniqueid_;
    std::string client_devicename_;
    std::string last_validated_ip_;  // IP of validated client
    
    // Auto-detected stream sample rate
    uint32_t detected_stream_rate_ = 0;
    int system_sample_rate_ = 0;  // Auto-detected system output rate (48k, 96k, etc)
    bool rate_logged_ = false;
    
    // Connection timeout tracking
    std::chrono::steady_clock::time_point last_packet_time_;
    std::chrono::steady_clock::time_point last_validated_time_;
    static constexpr int CONNECTION_TIMEOUT_MS = 2000;  // 2 seconds without packets = disconnected
    
    // Audio buffers
    static constexpr size_t MAX_FRAMES = 5760;  // 120ms at 48kHz
    float decode_buffer_[MAX_FRAMES * 2];  // Decoded audio at 16kHz
    float resample_buffer_[MAX_FRAMES * 2];  // Resampled audio at 48kHz

    std::mutex audio_mutex_; // Protects virtual_device_ and resampler_
};

} // namespace moonmic
