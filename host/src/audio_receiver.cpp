
#include "audio_receiver.h"
#include "../../moonmic_internal.h"
#include "debug.h"
#include "typing_focus.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include "platform/windows/audio_utils.h"
#endif

namespace moonmic {

static_assert(sizeof(moonmic_focus_request_t) == 32);
static_assert(sizeof(moonmic_focus_response_t) == 20);

AudioReceiver::AudioReceiver()
    : sunshine_(nullptr)
    , decoder_(nullptr)
    , resampler_(nullptr)
    , receiver_(nullptr)
    , virtual_device_(nullptr)
    , connection_monitor_(nullptr)
    , running_(false)
    , paused_(false)
    , client_validated_(false)
    , detected_stream_rate_(0)
    , system_sample_rate_(0)
    , rate_logged_(false) {
    memset(&stats_, 0, sizeof(stats_));
}

AudioReceiver::~AudioReceiver() {
    stop();
}

void AudioReceiver::resetConnectionState() {
    client_validated_ = false;
    stats_.is_connected = false;
    stats_.is_receiving = false;
    last_validated_ip_.clear();
    last_validated_time_ = std::chrono::steady_clock::time_point{};
    if (connection_monitor_) {
        connection_monitor_->stop();
    }

    if (virtual_device_) {

        virtual_device_->close();
        virtual_device_.reset();

        virtual_device_ = VirtualDevice::create();

        std::string output_device = config_.audio.use_speaker_mode ? "" : config_.audio.recording_endpoint_name;

        if (!virtual_device_->init(output_device, 0, config_.audio.channels)) {
            std::cerr << "[AudioReceiver] Failed to recreate virtual device on reset" << std::endl;
        } else {
             system_sample_rate_ = virtual_device_->getSampleRate();
             std::cout << "[AudioReceiver] Audio device reset. Rate: " << system_sample_rate_ << "Hz" << std::endl;
        }
    }

    if (resampler_) {
        speex_resampler_destroy(resampler_);
        resampler_ = nullptr;
    }
    detected_stream_rate_ = 0;
    rate_logged_ = false;
}

bool AudioReceiver::start(const Config& config) {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    if (running_) {
        return false;
    }

    config_ = config;

    config_ = config;

    virtual_device_ = VirtualDevice::create();
    std::string output_device = config_.audio.use_speaker_mode ? "" : config_.audio.recording_endpoint_name;
    std::string output_mode = config_.audio.use_speaker_mode ? "speakers (debug)" : config_.audio.recording_endpoint_name;

    if (!virtual_device_->init(
        output_device,
        0,
        config_.audio.channels
    )) {
        std::cerr << "[AudioReceiver] Failed to initialize audio device" << std::endl;
        return false;
    }

    system_sample_rate_ = virtual_device_->getSampleRate();
    std::cout << "[AudioReceiver] Audio output: " << output_mode
              << " @ " << system_sample_rate_ << "Hz (auto-detected)" << std::endl;

    int decoder_rate = (config_.audio.resampling_rate > 0) ? config_.audio.resampling_rate : system_sample_rate_;

    decoder_ = std::make_unique<FFmpegDecoder>();
    if (!decoder_->init(decoder_rate, config_.audio.channels)) {
        std::cerr << "[AudioReceiver] Failed to initialize FFmpeg Opus decoder" << std::endl;
        return false;
    }
    std::cout << "[AudioReceiver] FFmpeg Opus decoder initialized at " << decoder_rate << "Hz" << std::endl;

    if (config_.audio.resampling_rate == 0) {
        config_.audio.resampling_rate = decoder_rate;
    }

    resampler_ = nullptr;

    receiver_ = std::make_unique<UDPReceiver>();
    receiver_->setPacketCallback([this](const uint8_t* data, size_t size, const std::string& ip, uint16_t port, bool is_lagging) {
        onPacketReceived(data, size, ip, port, is_lagging);
    });

    if (!receiver_->start(config_.server.port, config_.server.bind_address)) {
        std::cerr << "[AudioReceiver] Failed to start UDP receiver" << std::endl;
        return false;
    }

    running_ = true;
    std::cout << "[AudioReceiver] Started successfully" << std::endl;
    return true;
}

void AudioReceiver::stop() {
    std::cout << "[AudioReceiver] stop() called" << std::endl;
    std::lock_guard<std::mutex> lock(audio_mutex_);
    if (!running_) {
        std::cout << "[AudioReceiver] Already stopped" << std::endl;
        return;
    }

    running_ = false;

    if (receiver_) {
        std::cout << "[AudioReceiver] Stopping UDP receiver..." << std::endl;
        receiver_->stop();
        receiver_.reset();
    }

    if (virtual_device_) {
        std::cout << "[AudioReceiver] Closing virtual device..." << std::endl;
        virtual_device_->close();
        virtual_device_.reset();
    }

    if (resampler_) {
        speex_resampler_destroy(resampler_);
        resampler_ = nullptr;
    }

    detected_stream_rate_ = 0;
    system_sample_rate_ = 0;
    rate_logged_ = false;

    if (decoder_) {
        decoder_.reset();
    }

    if (connection_monitor_) {
        std::cout << "[AudioReceiver] Stopping connection monitor..." << std::endl;
        connection_monitor_->stop();
        connection_monitor_.reset();
    }

#ifdef _WIN32

    if (!config_.audio.original_mic_id.empty()) {
        std::cout << "[AudioReceiver] Restoring original default microphone..." << std::endl;
        if (moonmic::platform::windows::SetDefaultRecordingDevice(config_.audio.original_mic_id)) {
            std::cout << "[AudioReceiver] Original microphone restored successfully" << std::endl;
        }
        config_.audio.original_mic_id = "";
        config_.save(Config::getDefaultConfigPath());
    }
#endif

    std::cout << "[AudioReceiver] Stopped" << std::endl;
}

void AudioReceiver::pause() {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    pauseInternal();
}

void AudioReceiver::pauseInternal() {
    if (!running_ || paused_) return;

    paused_ = true;
    stats_.is_paused = true;

    sendControlSignalInternal(MOONMIC_CTRL_STOP);

    std::cout << "[AudioReceiver] Paused - sent STOP to client" << std::endl;
}

void AudioReceiver::resume() {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    resumeInternal();
}

void AudioReceiver::resumeInternal() {
    if (!running_ || !paused_) return;

    paused_ = false;
    stats_.is_paused = false;

    last_packet_time_ = std::chrono::steady_clock::now();

    sendControlSignalInternal(MOONMIC_CTRL_START);

    std::cout << "[AudioReceiver] Resumed - sent START to client" << std::endl;
}

void AudioReceiver::sendControlSignal(uint32_t signal_magic) {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    sendControlSignalInternal(signal_magic);
}

void AudioReceiver::sendControlSignalInternal(uint32_t signal_magic) {
    if (!connection_monitor_ || last_validated_ip_.empty()) {
        std::cerr << "[AudioReceiver] Cannot send control signal: no validated client" << std::endl;
        return;
    }

    if (!connection_monitor_->isRunning()) {
        std::cerr << "[AudioReceiver] Cannot send control signal: connection monitor not running" << std::endl;
        return;
    }

    moonmic_control_packet_t packet;
    packet.magic = signal_magic;
    packet.reserved = 0;

    connection_monitor_->sendPacket(&packet, sizeof(packet));

    const char* signal_name = (signal_magic == MOONMIC_CTRL_STOP) ? "STOP" :
                              (signal_magic == MOONMIC_CTRL_START) ? "START" : "UNKNOWN";

    std::cout << "[AudioReceiver] Sent control signal: " << signal_name
              << " to " << last_validated_ip_ << std::endl;
}

bool AudioReceiver::switchAudioOutput(bool use_speakers) {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    if (!running_) return false;

    std::cout << "[AudioReceiver] Hot-swapping audio to "
              << (use_speakers ? "speakers" : "VB-Cable") << std::endl;

    bool was_paused = paused_;
    if (!was_paused) pauseInternal();

    if (virtual_device_) {
        virtual_device_->close();
        virtual_device_.reset();
    }

    if (resampler_) {
        speex_resampler_destroy(resampler_);
        resampler_ = nullptr;
    }
    detected_stream_rate_ = 0;
    rate_logged_ = false;

    config_.audio.use_speaker_mode = use_speakers;

#ifdef _WIN32

    if (use_speakers) {

        if (!config_.audio.original_mic_id.empty()) {
            std::cout << "[AudioReceiver] Speaker Mode: Restoring original default microphone..." << std::endl;
            if (moonmic::platform::windows::SetDefaultRecordingDevice(config_.audio.original_mic_id)) {
                std::cout << "[AudioReceiver] Original microphone restored." << std::endl;
            }
            config_.audio.original_mic_id = "";
            config_.save(Config::getDefaultConfigPath());
        }
    } else {

        std::string currentId, currentName;
        if (moonmic::platform::windows::GetDefaultRecordingDevice(currentId, currentName)) {
             std::string virtualId = moonmic::platform::windows::FindRecordingDeviceID(config_.audio.recording_endpoint_name);

             if (currentId != virtualId) {
                 std::cout << "[AudioReceiver] Virtual Mic Mode: Saving original default mic: " << currentName << std::endl;
                 config_.audio.original_mic_id = currentId;
                 config_.save(Config::getDefaultConfigPath());

                 if (moonmic::platform::windows::SetDefaultRecordingDevice(config_.audio.recording_endpoint_name)) {
                     std::cout << "[AudioReceiver] Set default mic to: " << config_.audio.recording_endpoint_name << std::endl;
                 }
             }
        }
    }
#endif

    std::string output_device = use_speakers ? "" : config_.audio.recording_endpoint_name;
    std::string output_mode = use_speakers ? "speakers (debug)" : config_.audio.recording_endpoint_name;

    virtual_device_ = VirtualDevice::create();

    if (!virtual_device_->init(output_device, 0, config_.audio.channels)) {
        std::cerr << "[AudioReceiver] Failed to initialize new audio device" << std::endl;
        if (!was_paused) resumeInternal();
        return false;
    }

    system_sample_rate_ = virtual_device_->getSampleRate();
    std::cout << "[AudioReceiver] Audio output: " << output_mode
              << " @ " << system_sample_rate_ << "Hz" << std::endl;

    if (!was_paused) resumeInternal();

    return true;
}

bool AudioReceiver::isClientAllowed(const std::string& ip) {

    if (!config_.security.enable_whitelist) {
        return true;
    }

    for (const auto& allowed_ip : config_.security.allowed_clients) {
        if (ip == allowed_ip) {
            return true;
        }
    }

    return false;
}

void AudioReceiver::onPacketReceived(const uint8_t* data, size_t size, const std::string& sender_ip, uint16_t sender_port, bool is_lagging) {
    std::lock_guard<std::mutex> lock(audio_mutex_);

    uint32_t packet_magic = 0;
    if (size >= sizeof(packet_magic)) {
        memcpy(&packet_magic, data, sizeof(packet_magic));
    }
    if (packet_magic == MOONMIC_FOCUS_REQUEST_MAGIC && size == sizeof(moonmic_focus_request_t)) {
        if (!receiver_ || !isClientAllowed(sender_ip)) {
            return;
        }

        const auto* request = reinterpret_cast<const moonmic_focus_request_t*>(data);
        if (request->version != MOONMIC_FOCUS_PROTOCOL_VERSION ||
            (config_.security.enable_whitelist && request->pair_status != 1)) {
            return;
        }

        TypingFocus focus;
        if (!getTypingFocus(focus)) {
            return;
        }

        moonmic_focus_response_t response = {};
        response.magic = MOONMIC_FOCUS_RESPONSE_MAGIC;
        response.version = MOONMIC_FOCUS_PROTOCOL_VERSION;
        response.source = focus.source;
        response.normalized_x = focus.normalized_x;
        response.normalized_y = focus.normalized_y;
        response.request_id = request->request_id;
        receiver_->sendTo(&response, sizeof(response), sender_ip, sender_port);
        return;
    }
    stats_.packets_received++;
    stats_.bytes_received += size;
    stats_.last_sender_ip = sender_ip;
    stats_.is_receiving = true;
    last_packet_time_ = std::chrono::steady_clock::now();

    const bool is_handshake_magic = (size >= sizeof(MoonmicHandshake)) &&
        (((const MoonmicHandshake*)data)->magic == 0x4D4F4F4E || ((const MoonmicHandshake*)data)->magic == 0x4E4F4F4D);

    if (is_handshake_magic) {

        resetConnectionState();

        uint16_t current_w = 0, current_h = 0;
        if (!validateHandshake(data, size, sender_ip, current_w, current_h)) {
            stats_.packets_dropped++;
            return;
        }

        client_validated_ = true;
        last_validated_ip_ = sender_ip;
        stats_.is_connected = true;
        last_validated_time_ = std::chrono::steady_clock::now();

        if (!connection_monitor_) {
            connection_monitor_ = std::make_unique<ConnectionMonitor>();
        }

        connection_monitor_->start(sender_ip, sender_port);
        std::cout << "[AudioReceiver] Started heartbeat monitor for " << sender_ip << ":" << sender_port << std::endl;

        // Because the client's moonmic_handshake_t is larger than our local definition
        uint8_t ack_buffer[256];
        memcpy(ack_buffer, data, std::min(size, sizeof(ack_buffer)));

        MoonmicHandshake* ack = (MoonmicHandshake*)ack_buffer;
        ack->magic = 0x4B434148;
        if (current_w > 0 && current_h > 0) {
            ack->display_width = current_w;
            ack->display_height = current_h;
        }

        connection_monitor_->sendPacket(ack_buffer, size);
        std::cout << "[AudioReceiver] Sent Handshake ACK (" << size << " bytes) to " << sender_ip << std::endl;

        return;
    }

    const uint32_t PACKET_MAGIC_PING = 0x50494E47;

    const uint32_t PACKET_MAGIC_PONG = 0x504F4E47;

    if (size >= 12) {
        uint32_t magic;
        memcpy(&magic, data, 4);

        if (magic == PACKET_MAGIC_PING) {

             if (receiver_) {

                 std::vector<uint8_t> pong(size);
                 memcpy(pong.data(), data, size);
                 uint32_t pong_magic = PACKET_MAGIC_PONG;
                 memcpy(pong.data(), &pong_magic, 4);

                 receiver_->sendTo(pong.data(), size, sender_ip, sender_port);
             }
             return;
        } else if (magic == PACKET_MAGIC_PONG) {

             uint64_t timestamp;
             memcpy(&timestamp, data + 4, 8);

             auto now = std::chrono::system_clock::now();
             auto duration = now.time_since_epoch();
             uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

             int64_t diff_us = (int64_t)(now_us - timestamp);

             if (diff_us >= 0 && diff_us < 5000000) {
                 stats_.rtt_ms = (int)(diff_us / 1000);
             }

             stats_.last_sender_ip = sender_ip;
             stats_.is_receiving = true;
             last_packet_time_ = std::chrono::steady_clock::now();

             return;
        }
    }

    if (size < MOONMIC_HEADER_SIZE) {
        std::cerr << "[AudioReceiver] Packet too small: " << size << " bytes (expected at least " << MOONMIC_HEADER_SIZE << " for header)" << std::endl;
        stats_.packets_dropped++;
        return;
    }

    uint32_t magic = ((uint32_t)data[0] << 0) | ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);

    // Validate magic - must be MMIC for audio packets

    if (magic != MOONMIC_MAGIC) {

        return;
    }

    if (is_lagging) {
        static int lag_drop_counter = 0;
        lag_drop_counter++;
        if (lag_drop_counter % 50 == 0) {
             std::cout << "[AudioReceiver] ⚠ LAG DETECTED: Dropping packet to drain buffer (Backlog > 2048 bytes)" << std::endl;
        }
        stats_.packets_dropped++;
        stats_.packets_dropped_lag++;
        return;
    }

    uint32_t sequence = ((uint32_t)data[4] << 0) | ((uint32_t)data[5] << 8) |
                        ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);

    uint64_t timestamp = ((uint64_t)data[8] << 0) | ((uint64_t)data[9] << 8) |
                         ((uint64_t)data[10] << 16) | ((uint64_t)data[11] << 24) |
                         ((uint64_t)data[12] << 32) | ((uint64_t)data[13] << 40) |
                         ((uint64_t)data[14] << 48) | ((uint64_t)data[15] << 56);

    uint32_t sample_rate_field = ((uint32_t)data[16] << 0) |
                                  ((uint32_t)data[17] << 8) |
                                  ((uint32_t)data[18] << 16) |
                                  ((uint32_t)data[19] << 24);

    bool is_raw_mode = (sample_rate_field & MOONMIC_RAW_FLAG) != 0;
    uint32_t stream_rate = sample_rate_field & ~MOONMIC_RAW_FLAG;

    if (stats_.packets_received == 1) {
        std::cout << "[AudioReceiver] FIRST PACKET DEBUG (manual read):" << std::endl;
        std::cout << "  Packet size: " << size << " bytes" << std::endl;
        std::cout << "  magic = 0x" << std::hex << magic << " (expected 0x" << MOONMIC_MAGIC << ")" << std::dec << std::endl;
        std::cout << "  sequence = " << sequence << std::endl;
        std::cout << "  timestamp = " << timestamp << std::endl;
        std::cout << "  sample_rate = " << stream_rate << std::endl;
        std::cout << "  raw_mode = " << (is_raw_mode ? "YES" : "NO") << std::endl;
        std::cout << "  Raw header bytes:";
        for (int i = 0; i < 20; i++) {
            std::cout << " " << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
        }
        std::cout << std::dec << std::endl << std::endl;
    }

    if (detected_stream_rate_ == 0) {
        detected_stream_rate_ = stream_rate;
        rate_logged_ = true;

        std::cout << "[AudioReceiver] ═══ Stream Detected ═══" << std::endl;
        std::cout << "[AudioReceiver] Source IP: " << sender_ip << std::endl;
        std::cout << "[AudioReceiver] Stream sample rate: " << stream_rate << " Hz" << std::endl;
        std::cout << "[AudioReceiver] Output sample rate: " << system_sample_rate_ << " Hz" << std::endl;
        std::cout << "[AudioReceiver] Mode: " << (is_raw_mode ? "RAW PCM" : "Opus") << std::endl;

        if (!resampler_ || stream_rate != detected_stream_rate_) {

            int err = 0;
            if (resampler_) speex_resampler_destroy(resampler_);

            resampler_ = speex_resampler_init(
                config_.audio.channels,
                stream_rate,
                system_sample_rate_,
                10,
                &err
            );

            if (err != RESAMPLER_ERR_SUCCESS || !resampler_) {
                std::cerr << "[AudioReceiver] Failed to create resampler: " << err << std::endl;
                return;
            }

            std::cout << "[AudioReceiver] ✓ Resampler active: " << stream_rate << "Hz → "
                      << system_sample_rate_ << "Hz (quality 10)" << std::endl;
            if (stream_rate == system_sample_rate_) {
                std::cout << "[AudioReceiver] (Resampler enabled for Drift Correction)" << std::endl;
            }
        }
        std::cout << "[AudioReceiver] ═══════════════════════\n" << std::endl;
    }

    const uint8_t* payload = data + MOONMIC_HEADER_SIZE;
    size_t payload_size = size - MOONMIC_HEADER_SIZE;

    float* output_buffer = decode_buffer_;
    int output_frames = 0;

    if (is_raw_mode) {

        const int16_t* pcm_int16 = (const int16_t*)payload;
        int num_samples = payload_size / sizeof(int16_t);
        output_frames = num_samples / config_.audio.channels;

        static bool first_raw_logged = false;
        if (!first_raw_logged) first_raw_logged = true;

        for (int i = 0; i < num_samples; i++) {
            decode_buffer_[i] = (float)pcm_int16[i] / 32768.0f;
        }

        if (resampler_) {

            static int packet_counter = 0;
            if (++packet_counter % 10 == 0) {
                float usage = virtual_device_->getBufferUsage();

                spx_uint32_t in_rate, out_rate;
                speex_resampler_get_rate(resampler_, &in_rate, &out_rate);

                uint32_t base_rate = system_sample_rate_;

                float error = usage - 0.5f;

                if (std::abs(error) > 0.05f) {

                    const int Kp = 5000;

                    int correction = (int)(error * Kp);

                    int target_rate = (int)base_rate - correction;

                    if (target_rate > (int)base_rate + 4000) target_rate = base_rate + 4000;
                    if (target_rate < (int)base_rate - 4000) target_rate = base_rate - 4000;

                    if (std::abs(target_rate - (int)out_rate) > 10) {

                         speex_resampler_set_rate(resampler_, in_rate, (spx_uint32_t)target_rate);
                    }
                }
            }

            spx_uint32_t in_len = output_frames;

#ifdef _WIN32

            // We check this here because this is where we know audio is flowing

            if (!config_.audio.use_speaker_mode) {
                std::string currentId, currentName;
                if (moonmic::platform::windows::GetDefaultRecordingDevice(currentId, currentName)) {

                    std::string virtualId = moonmic::platform::windows::FindRecordingDeviceID(config_.audio.recording_endpoint_name);

                    if (!virtualId.empty() && currentId != virtualId) {
                        std::cout << "[AudioReceiver] Saving original default mic: " << currentName << " (" << currentId << ")" << std::endl;
                        config_.audio.original_mic_id = currentId;

                        config_.save(Config::getDefaultConfigPath());

                        if (moonmic::platform::windows::SetDefaultRecordingDevice(config_.audio.recording_endpoint_name)) {
                            std::cout << "[AudioReceiver] Auto-set default mic to: " << config_.audio.recording_endpoint_name << std::endl;
                        }
                    }
                }
            }
#endif

            spx_uint32_t out_len = MAX_FRAMES;

            static bool first_resample_logged = false;
            if (!first_resample_logged) first_resample_logged = true;

            int err = speex_resampler_process_float(
                resampler_,
                0,
                decode_buffer_,
                &in_len,
                resample_buffer_,
                &out_len
            );

            if (!first_resample_logged) {

                first_resample_logged = true;
            }

            if (err != RESAMPLER_ERR_SUCCESS) {
                stats_.packets_dropped++;
                std::cerr << "[AudioReceiver] RAW resampling failed: " << err << std::endl;
                return;
            }

            output_buffer = resample_buffer_;
            output_frames = out_len;
        }

        static uint64_t raw_packet_count = 0;
        if (++raw_packet_count % 100 == 1 && raw_packet_count > 1) {
            if (isDebugMode()) {
                std::cout << "[AudioReceiver] Processing RAW: packet #" << stats_.packets_received
                          << ", " << output_frames << " frames" << std::endl;
            }
        }
    } else {

        int decoded_frames = decoder_->decode(payload, payload_size, decode_buffer_, MAX_FRAMES);
        if (decoded_frames < 0) {
            stats_.packets_dropped++;
            std::cerr << "[AudioReceiver] Decode failed for packet from " << sender_ip << std::endl;
            return;
        }

        output_frames = decoded_frames;

        if (system_sample_rate_ != detected_stream_rate_ && resampler_) {
            spx_uint32_t in_len = decoded_frames;
            spx_uint32_t out_len = MAX_FRAMES;

            int err = speex_resampler_process_float(
                resampler_,
                0,
                decode_buffer_,
                &in_len,
                resample_buffer_,
                &out_len
            );

            if (err != RESAMPLER_ERR_SUCCESS) {
                stats_.packets_dropped++;
                std::cerr << "[AudioReceiver] Resampling failed: " << err << std::endl;
                return;
            }

            output_buffer = resample_buffer_;
            output_frames = out_len;
        }

        static uint64_t packet_count = 0;
        if (++packet_count % 100 == 1 && packet_count > 1) {
            if (config_.audio.resampling_rate == detected_stream_rate_) {
                if (isDebugMode()) {
                    std::cout << "[AudioReceiver] Processing Opus: packet #" << stats_.packets_received
                              << ", decoded " << output_frames << " frames" << std::endl;
                }
            } else {
                if (isDebugMode()) {
                    std::cout << "[AudioReceiver] Processing Opus: packet #" << stats_.packets_received
                              << ", decoded=" << decoded_frames << " frames @ " << detected_stream_rate_ << "Hz"
                              << ", resampled=" << output_frames << " frames @ " << config_.audio.resampling_rate
                              << "Hz (Speex)" << std::endl;
                }
            }
        }
    }

    if (!config_.audio.use_speaker_mode && config_.audio.recording_endpoint_name.find("Steam") != std::string::npos) {
        static bool attenuation_logged = false;
        if (!attenuation_logged) {
            std::cout << "[AudioReceiver] Steam WDM-KS detected: applying 15% pre-attenuation to compensate for driver AGC" << std::endl;
            attenuation_logged = true;
        }

        const float STEAM_ATTENUATION = 0.15f;
        for (size_t i = 0; i < output_frames * config_.audio.channels; i++) {
            output_buffer[i] *= STEAM_ATTENUATION;
        }
    }

    if (!virtual_device_->write(output_buffer, output_frames, config_.audio.channels)) {

    }

    stats_.is_receiving = true;
}

bool AudioReceiver::validateHandshake(const uint8_t* data, size_t size, const std::string& sender_ip, uint16_t& out_w, uint16_t& out_h) {
    if (size < sizeof(MoonmicHandshake)) {
        std::cerr << "[AudioReceiver] Packet too small for handshake: " << size << " bytes" << std::endl;
        return false;
    }

    const MoonmicHandshake* hs = reinterpret_cast<const MoonmicHandshake*>(data);

    uint32_t magic = hs->magic;
    if (magic != 0x4D4F4F4E && magic != 0x4E4F4F4D) {
        std::cerr << "[AudioReceiver] Invalid handshake magic: 0x"
                  << std::hex << magic << std::dec << std::endl;
        return false;
    }

    if (hs->uniqueid_len > 0 && hs->uniqueid_len <= 16) {
        client_uniqueid_ = std::string(hs->uniqueid, hs->uniqueid_len);
    }

    if (hs->devicename_len > 0 && hs->devicename_len <= 64) {
        client_devicename_ = std::string(hs->devicename, hs->devicename_len);
        stats_.client_name = client_devicename_;
    }

    if (!config_.security.enable_whitelist) {
        std::cout << "[AudioReceiver] Client connected: " << client_devicename_ << " [whitelist disabled]" << std::endl;

        last_packet_time_ = std::chrono::steady_clock::now();
        stats_.is_connected = true;
        return true;
    }

    // NOTE: UUID verification is NOT possible because Sunshine generates a

    if (hs->pair_status != 1) {
        auto now = std::chrono::steady_clock::now();
        auto grace_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_validated_time_).count();
        bool grace = (last_validated_time_.time_since_epoch().count() != 0) &&
                     (last_validated_ip_ == sender_ip) &&
                     (grace_ms < 8000);

        if (grace) {
            std::cout << "[AudioReceiver] Grace-accept pair_status=0 during Sunshine restart (" << grace_ms << "ms since last validation)" << std::endl;
        } else {
            std::cerr << "[AudioReceiver] DENY: Client '" << client_devicename_
                      << "' not validated by Sunshine (pair_status=" << (int)hs->pair_status << ")" << std::endl;
            std::cerr << "[AudioReceiver] Ensure client is paired with Sunshine host" << std::endl;
            return false;
        }
    }

    std::cout << "[AudioReceiver] Client validated (pair_status=1): " << client_devicename_ << std::endl;

    if (hs->version >= 2 && hs->display_width > 0 && hs->display_height > 0) {
        std::cout << "[AudioReceiver] Client requests display resolution: "
                  << hs->display_width << "x" << hs->display_height << std::endl;

        out_w = 0; out_h = 0;
        if (sunshine_webui_) {
            sunshine_webui_->getCurrentResolution(out_w, out_h);
        }

        bool force_update = (hs->flags & 0x01);
        bool should_update = true;

        if (out_w > 0 && out_h > 0 && !force_update) {
            if (out_w != hs->display_width || out_h != hs->display_height) {
                std::cout << "[AudioReceiver] Resolution mismatch (Current: " << out_w << "x" << out_h
                          << ", Target: " << hs->display_width << "x" << hs->display_height
                          << "). Waiting for FORCE flag." << std::endl;
                should_update = false;
            }
        }

        bool is_valid = false;
        if (hs->display_width == 1280 && hs->display_height == 720) is_valid = true;
        if (hs->display_width == 1600 && hs->display_height == 900) is_valid = true;
        if (hs->display_width == 1920 && hs->display_height == 1080) is_valid = true;
        if (hs->display_width == 2560 && hs->display_height == 1440) is_valid = true;
        if (hs->display_width == 3840 && hs->display_height == 2160) is_valid = true;

        if (is_valid && should_update) {
            if (!applyDisplayResolution(hs->display_width, hs->display_height)) {
                std::cerr << "[AudioReceiver] Warning: host resolution request could not be applied automatically" << std::endl;
            }
        } else if (!is_valid) {
            std::cerr << "[AudioReceiver] Invalid resolution request: "
                      << hs->display_width << "x" << hs->display_height << std::endl;
        }
    }

    return true;
}

bool AudioReceiver::applyDisplayResolution(uint16_t width, uint16_t height) {
    bool applied = false;
    bool attempted_sunshine = false;

    if (sunshine_webui_) {
        attempted_sunshine = true;

        uint16_t current_w = 0, current_h = 0;
        bool has_current = sunshine_webui_->getCurrentResolution(current_w, current_h);
        bool already_correct = has_current && (current_w == width) && (current_h == height);

        if (already_correct) {
            std::cout << "[AudioReceiver] Resolution already set to " << width << "x" << height
                      << " - No restart needed" << std::endl;
            return true;
        }

        if (sunshine_webui_->setDisplayResolution(width, height)) {
            std::cout << "[AudioReceiver] ✓ Sunshine configured for " << width << "x"
                      << height << " → 960x544 downscale (host mode intact)" << std::endl;
            applied = true;

            if (sunshine_webui_->restartSunshine()) {
                std::cout << "[AudioReceiver] Sunshine restart requested after resolution change" << std::endl;
            } else {
                std::cerr << "[AudioReceiver] Sunshine restart request failed" << std::endl;
            }
        } else {
            std::cerr << "[AudioReceiver] Sunshine WebUI failed to apply resolution" << std::endl;
        }
    } else {
        std::cerr << "[AudioReceiver] Sunshine WebUI not available - skipping API resolution change" << std::endl;
    }

    // Do not force host display changes; rely solely on Sunshine remapping.
    return applied;
}

bool AudioReceiver::applyFallbackDisplayResolution(uint16_t width, uint16_t height) {
    // Host resolution should remain untouched. Disable fallback.
    std::cout << "[AudioReceiver] Fallback display resolution disabled (host mode unchanged)" << std::endl;
    return false;
}

AudioReceiver::Stats AudioReceiver::getStats() {

    stats_.is_connected = client_validated_;
    stats_.is_paused = paused_;

    if (stats_.is_connected && connection_monitor_ && last_validated_time_.time_since_epoch().count() > 0) {
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_time_);

        if (diff.count() > 2000) {
             stats_.is_receiving = false;

             if (diff.count() > 4000) {
                 stats_.is_connected = false;

                 if (client_validated_) {
                     std::cout << "[AudioReceiver] Client disconnected (timeout): " << client_devicename_ << std::endl;
                     resetConnectionState();
                 }
             }
        }
    }
    return stats_;
}

}
