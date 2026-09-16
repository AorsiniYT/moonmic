#include "logger.h"

#include "audio_receiver.h"
#include "debug.h"
#include "typing_focus.h"
#include <limits>
#include <vector>

#ifdef _WIN32
#include "platform/windows/audio_utils.h"
#endif

namespace moonmic {

AudioReceiver::AudioReceiver()
    : decoder_(nullptr), receiver_(nullptr), virtual_device_(nullptr), connection_monitor_(nullptr),
      running_(false), paused_(false), client_validated_(false), detected_stream_rate_(0), system_sample_rate_(0) {
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
            moonmic::logError() << "[AudioReceiver] Failed to recreate virtual device on reset" << std::endl;
        } else {
            system_sample_rate_ = virtual_device_->getSampleRate();
            moonmic::logInfo() << "[AudioReceiver] Audio device reset. Rate: " << system_sample_rate_ << "Hz" << std::endl;
        }
    }

    resampler_.reset();
    detected_stream_rate_ = 0;
}

bool AudioReceiver::start(const Config& config) {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    if (running_ || stopping_) {
        return false;
    }
    if (config.audio.channels < 1 || config.audio.channels > 2) {
        moonmic::logError() << "[AudioReceiver] Unsupported channel count: " << config.audio.channels << std::endl;
        return false;
    }

    config_ = config;
    stats_ = {};
    paused_ = false;
    client_validated_ = false;
    lag_drop_count_ = 0;
    raw_packet_count_ = 0;
    opus_packet_count_ = 0;
    resampler_packet_count_ = 0;
    steam_attenuation_logged_ = false;

    virtual_device_ = VirtualDevice::create();
    std::string output_device = config_.audio.use_speaker_mode ? "" : config_.audio.recording_endpoint_name;
    std::string output_mode =
        config_.audio.use_speaker_mode ? "speakers (debug)" : config_.audio.recording_endpoint_name;

    if (!virtual_device_->init(output_device, 0, config_.audio.channels)) {
        moonmic::logError() << "[AudioReceiver] Failed to initialize audio device" << std::endl;
        virtual_device_.reset();
        return false;
    }

    system_sample_rate_ = virtual_device_->getSampleRate();
    if (system_sample_rate_ > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        moonmic::logError() << "[AudioReceiver] Unsupported output sample rate: " << system_sample_rate_ << std::endl;
        virtual_device_->close();
        virtual_device_.reset();
        return false;
    }
    moonmic::logInfo() << "[AudioReceiver] Audio output: " << output_mode << " @ " << system_sample_rate_ << "Hz (auto-detected)"
              << std::endl;

    int decoder_rate =
        (config_.audio.resampling_rate > 0) ? config_.audio.resampling_rate : static_cast<int>(system_sample_rate_);

    decoder_ = std::make_unique<FFmpegDecoder>();
    if (!decoder_->init(decoder_rate, config_.audio.channels)) {
        moonmic::logError() << "[AudioReceiver] Failed to initialize FFmpeg Opus decoder" << std::endl;
        decoder_.reset();
        virtual_device_->close();
        virtual_device_.reset();
        return false;
    }
    moonmic::logInfo() << "[AudioReceiver] FFmpeg Opus decoder initialized at " << decoder_rate << "Hz" << std::endl;

    if (config_.audio.resampling_rate == 0) {
        config_.audio.resampling_rate = decoder_rate;
    }

    resampler_.reset();

    receiver_ = std::make_unique<UDPReceiver>();
    receiver_->setPacketCallback([this](const uint8_t* data, size_t size, const std::string& ip, uint16_t port,
                                        bool is_lagging) { onPacketReceived(data, size, ip, port, is_lagging); });

    if (!receiver_->start(config_.server.port, config_.server.bind_address)) {
        moonmic::logError() << "[AudioReceiver] Failed to start UDP receiver" << std::endl;
        receiver_.reset();
        decoder_.reset();
        virtual_device_->close();
        virtual_device_.reset();
        return false;
    }

    running_ = true;
    moonmic::logInfo() << "[AudioReceiver] Listening on " << config_.server.bind_address << ":" << config_.server.port
                       << std::endl;
    return true;
}

void AudioReceiver::stop() {
    std::unique_ptr<UDPReceiver> receiver;
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (stopping_) {
            return;
        }
        if (!running_ && !receiver_ && !virtual_device_ && !decoder_ && !connection_monitor_ &&
            !resampler_.isInitialized()) {
            return;
        }

        stopping_ = true;
        running_ = false;
        receiver = std::move(receiver_);
    }

    if (receiver) receiver->stop();

    std::lock_guard<std::mutex> lock(audio_mutex_);

    if (virtual_device_) {
        virtual_device_->close();
        virtual_device_.reset();
    }

    resampler_.reset();

    detected_stream_rate_ = 0;
    system_sample_rate_ = 0;

    if (decoder_) {
        decoder_.reset();
    }

    if (connection_monitor_) {
        connection_monitor_->stop();
        connection_monitor_.reset();
    }

#ifdef _WIN32

    if (!config_.audio.original_mic_id.empty()) {
        moonmic::logInfo() << "[AudioReceiver] Restoring original default microphone..." << std::endl;
        if (moonmic::platform::windows::SetDefaultRecordingDevice(config_.audio.original_mic_id)) {
            moonmic::logInfo() << "[AudioReceiver] Original microphone restored successfully" << std::endl;
        }
        config_.audio.original_mic_id = "";
        config_.save(Config::getDefaultConfigPath());
    }
#endif

    paused_ = false;
    client_validated_ = false;
    stats_.is_connected = false;
    stats_.is_receiving = false;
    stats_.is_paused = false;
    stopping_ = false;
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

    moonmic::logInfo() << "[AudioReceiver] Paused - sent STOP to client" << std::endl;
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

    moonmic::logInfo() << "[AudioReceiver] Resumed - sent START to client" << std::endl;
}

void AudioReceiver::sendControlSignal(uint32_t signal_magic) {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    sendControlSignalInternal(signal_magic);
}

void AudioReceiver::sendControlSignalInternal(uint32_t signal_magic) {
    if (!connection_monitor_ || last_validated_ip_.empty()) {
        moonmic::logError() << "[AudioReceiver] Cannot send control signal: no validated client" << std::endl;
        return;
    }

    if (!connection_monitor_->isRunning()) {
        moonmic::logError() << "[AudioReceiver] Cannot send control signal: connection monitor not running" << std::endl;
        return;
    }

    uint8_t packet[MOONMIC_CONTROL_SIZE];
    moonmic_write_control_le(packet, signal_magic);

    connection_monitor_->sendPacket(packet, sizeof(packet));

    const char* signal_name = (signal_magic == MOONMIC_CTRL_STOP)    ? "STOP"
                              : (signal_magic == MOONMIC_CTRL_START) ? "START"
                                                                     : "UNKNOWN";

    moonmic::logInfo() << "[AudioReceiver] Sent control signal: " << signal_name << " to " << last_validated_ip_ << std::endl;
}

bool AudioReceiver::switchAudioOutput(bool use_speakers) {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    if (!running_) return false;

    moonmic::logInfo() << "[AudioReceiver] Hot-swapping audio to " << (use_speakers ? "speakers" : "VB-Cable") << std::endl;

    bool was_paused = paused_;
    if (!was_paused) pauseInternal();

    if (virtual_device_) {
        virtual_device_->close();
        virtual_device_.reset();
    }

    resampler_.reset();
    detected_stream_rate_ = 0;

    config_.audio.use_speaker_mode = use_speakers;

#ifdef _WIN32

    if (use_speakers) {

        if (!config_.audio.original_mic_id.empty()) {
            moonmic::logInfo() << "[AudioReceiver] Speaker Mode: Restoring original default microphone..." << std::endl;
            if (moonmic::platform::windows::SetDefaultRecordingDevice(config_.audio.original_mic_id)) {
                moonmic::logInfo() << "[AudioReceiver] Original microphone restored." << std::endl;
            }
            config_.audio.original_mic_id = "";
            config_.save(Config::getDefaultConfigPath());
        }
    } else {

        std::string currentId, currentName;
        if (moonmic::platform::windows::GetDefaultRecordingDevice(currentId, currentName)) {
            std::string virtualId =
                moonmic::platform::windows::FindRecordingDeviceID(config_.audio.recording_endpoint_name);

            if (currentId != virtualId) {
                moonmic::logInfo() << "[AudioReceiver] Virtual Mic Mode: Saving original default mic: " << currentName
                          << std::endl;
                config_.audio.original_mic_id = currentId;
                config_.save(Config::getDefaultConfigPath());

                if (moonmic::platform::windows::SetDefaultRecordingDevice(config_.audio.recording_endpoint_name)) {
                    moonmic::logInfo() << "[AudioReceiver] Set default mic to: " << config_.audio.recording_endpoint_name
                              << std::endl;
                }
            }
        }
    }
#endif

    std::string output_device = use_speakers ? "" : config_.audio.recording_endpoint_name;
    std::string output_mode = use_speakers ? "speakers (debug)" : config_.audio.recording_endpoint_name;

    virtual_device_ = VirtualDevice::create();

    if (!virtual_device_->init(output_device, 0, config_.audio.channels)) {
        moonmic::logError() << "[AudioReceiver] Failed to initialize new audio device" << std::endl;
        if (!was_paused) resumeInternal();
        return false;
    }

    system_sample_rate_ = virtual_device_->getSampleRate();
    moonmic::logInfo() << "[AudioReceiver] Audio output: " << output_mode << " @ " << system_sample_rate_ << "Hz" << std::endl;

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

void AudioReceiver::onPacketReceived(const uint8_t* data, size_t size, const std::string& sender_ip,
                                     uint16_t sender_port, bool is_lagging) {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    if (!running_) {
        return;
    }

    uint32_t packet_magic = size >= sizeof(uint32_t) ? moonmic_read_u32_le(data) : 0;
    if (packet_magic == MOONMIC_FOCUS_REQUEST_MAGIC && size == MOONMIC_FOCUS_REQUEST_SIZE) {
        if (!receiver_ || !isClientAllowed(sender_ip)) {
            return;
        }

        moonmic_focus_request_t request = {};
        moonmic_read_focus_request_le(data, &request);
        if (request.version != MOONMIC_FOCUS_PROTOCOL_VERSION ||
            (config_.security.enable_whitelist && request.pair_status != 1)) {
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
        response.request_id = request.request_id;
        uint8_t response_wire[MOONMIC_FOCUS_RESPONSE_SIZE];
        moonmic_write_focus_response_le(response_wire, &response);
        receiver_->sendTo(response_wire, sizeof(response_wire), sender_ip, sender_port);
        return;
    }
    stats_.packets_received++;
    stats_.bytes_received += size;
    stats_.last_sender_ip = sender_ip;
    stats_.is_receiving = true;
    last_packet_time_ = std::chrono::steady_clock::now();

    const bool is_handshake_magic =
        (size >= MOONMIC_HANDSHAKE_SIZE) && moonmic_read_u32_le(data) == MOONMIC_HANDSHAKE_MAGIC;

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
        // Replies must target the client's source port.
        connection_monitor_->start(sender_ip, sender_port);
        moonmic::logInfo() << "[AudioReceiver] Started heartbeat monitor for " << sender_ip << ":" << sender_port << std::endl;

        std::vector<uint8_t> ack_buffer(data, data + size);

        moonmic_handshake_t ack = {};
        moonmic_read_handshake_le(ack_buffer.data(), &ack);
        ack.magic = MOONMIC_HANDSHAKE_ACK;
        if (current_w > 0 && current_h > 0) {
            ack.display_width = current_w;
            ack.display_height = current_h;
        }

        uint8_t ack_wire[MOONMIC_HANDSHAKE_SIZE];
        moonmic_write_handshake_le(ack_wire, &ack);

        connection_monitor_->sendPacket(ack_wire, sizeof(ack_wire));
        moonmic::logInfo() << "[AudioReceiver] Sent Handshake ACK (" << size << " bytes) to " << sender_ip << std::endl;

        return;
    }

    if (size == MOONMIC_PING_SIZE) {
        uint32_t magic = moonmic_read_u32_le(data);

        if (magic == MOONMIC_PING_MAGIC) {
            if (receiver_) {
                uint8_t pong[MOONMIC_PING_SIZE];
                moonmic_write_ping_le(pong, MOONMIC_PONG_MAGIC, moonmic_read_ping_timestamp_le(data));
                receiver_->sendTo(pong, sizeof(pong), sender_ip, sender_port);
            }
            return;
        } else if (magic == MOONMIC_PONG_MAGIC) {
            uint64_t timestamp = moonmic_read_ping_timestamp_le(data);

            auto now = std::chrono::system_clock::now();
            auto duration = now.time_since_epoch();
            uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

            int64_t diff_us = (int64_t)(now_us - timestamp);

            if (diff_us >= 0 && diff_us < 3000000) {
                stats_.rtt_ms = (int)(diff_us / 1000);
            }

            stats_.last_sender_ip = sender_ip;
            stats_.is_receiving = true;
            last_packet_time_ = std::chrono::steady_clock::now();

            return;
        }
    }

    if (size < MOONMIC_HEADER_SIZE) {
        moonmic::logError() << "[AudioReceiver] Packet too small: " << size << " bytes (expected at least " << MOONMIC_HEADER_SIZE
                  << " for header)" << std::endl;
        stats_.packets_dropped++;
        return;
    }

    uint32_t magic = moonmic_read_u32_le(data);

    if (magic != MOONMIC_MAGIC) {
        return;
    }

    // Audio is only accepted from the peer that completed the handshake; with
    // the whitelist on, a stray datagram with a valid magic must not inject.
    if (config_.security.enable_whitelist && (!client_validated_ || sender_ip != last_validated_ip_)) {
        stats_.packets_dropped++;
        return;
    }

    if (is_lagging) {
        lag_drop_count_++;
        if (lag_drop_count_ % 50 == 0) {
            moonmic::logInfo() << "[AudioReceiver] Lag detected: dropping packet to drain the receive buffer" << std::endl;
        }
        stats_.packets_dropped++;
        stats_.packets_dropped_lag++;
        return;
    }

    uint32_t sample_rate_field = moonmic_read_u32_le(data + 16);

    bool is_raw_mode = (sample_rate_field & MOONMIC_RAW_FLAG) != 0;
    uint32_t stream_rate = sample_rate_field & ~MOONMIC_RAW_FLAG;

    if (stream_rate < 8000 || stream_rate > 192000) {
        stats_.packets_dropped++;
        moonmic::logError() << "[AudioReceiver] Unsupported stream sample rate: " << stream_rate << " Hz" << std::endl;
        return;
    }

    if (!resampler_.isInitialized() || detected_stream_rate_ != stream_rate) {
        const bool first_packet = detected_stream_rate_ == 0;
        const uint32_t previous_rate = detected_stream_rate_;
        const int error = resampler_.configure(config_.audio.channels, stream_rate, system_sample_rate_);
        if (error != RESAMPLER_ERR_SUCCESS) {
            stats_.packets_dropped++;
            moonmic::logError() << "[AudioReceiver] Failed to configure resampler: " << error << std::endl;
            return;
        }

        detected_stream_rate_ = stream_rate;
        resampler_packet_count_ = 0;

        auto stream_log = moonmic::logInfo();
        stream_log << "[AudioReceiver] " << sender_ip;
        if (first_packet) {
            stream_log << " connected with ";
        } else {
            stream_log << " changed from " << previous_rate << " Hz to ";
        }
        stream_log << stream_rate << " Hz " << (is_raw_mode ? "PCM" : "Opus") << ", output " << system_sample_rate_
                   << " Hz" << std::endl;
    }

    const uint8_t* payload = data + MOONMIC_HEADER_SIZE;
    size_t payload_size = size - MOONMIC_HEADER_SIZE;

    float* output_buffer = decode_buffer_;
    int output_frames = 0;

    if (is_raw_mode) {
        const size_t channels = static_cast<size_t>(config_.audio.channels);
        const size_t num_samples = payload_size / sizeof(int16_t);
        if (payload_size % sizeof(int16_t) != 0 || num_samples > MAX_FRAMES * 2 || num_samples % channels != 0) {
            stats_.packets_dropped++;
            moonmic::logError() << "[AudioReceiver] Invalid raw audio payload: " << payload_size << " bytes" << std::endl;
            return;
        }
        output_frames = static_cast<int>(num_samples / channels);

        // INT16_MIN requires a 32768 divisor to stay within [-1, 1].
        for (size_t i = 0; i < num_samples; i++) {
            uint16_t raw_sample = moonmic_read_u16_le(payload + (i * sizeof(uint16_t)));
            int32_t sample = raw_sample < 0x8000U ? raw_sample : static_cast<int32_t>(raw_sample) - 0x10000;
            decode_buffer_[i] = static_cast<float>(sample) / 32768.0f;
        }

        if (resampler_.isInitialized()) {

            if (++resampler_packet_count_ % 10 == 0) {
                float usage = virtual_device_->getBufferUsage();

                int base_rate = static_cast<int>(system_sample_rate_);

                float error = usage - 0.5f;

                if (std::abs(error) > 0.05f) {
                    const int Kp = 5000;

                    int correction = (int)(error * Kp);

                    int target_rate = base_rate - correction;

                    if (target_rate > base_rate + 4000) target_rate = base_rate + 4000;
                    if (target_rate < base_rate - 4000) target_rate = base_rate - 4000;

                    if (std::abs(target_rate - static_cast<int>(resampler_.outputRate())) > 10) {
                        resampler_.setRates(resampler_.inputRate(), static_cast<uint32_t>(target_rate));
                    }
                }
            }

            uint32_t in_len = static_cast<uint32_t>(output_frames);

#ifdef _WIN32
            if (!config_.audio.use_speaker_mode) {
                std::string currentId, currentName;
                if (moonmic::platform::windows::GetDefaultRecordingDevice(currentId, currentName)) {

                    std::string virtualId =
                        moonmic::platform::windows::FindRecordingDeviceID(config_.audio.recording_endpoint_name);

                    if (!virtualId.empty() && currentId != virtualId) {
                        moonmic::logInfo() << "[AudioReceiver] Saving original default mic: " << currentName << " (" << currentId
                                  << ")" << std::endl;
                        config_.audio.original_mic_id = currentId;

                        config_.save(Config::getDefaultConfigPath());

                        if (moonmic::platform::windows::SetDefaultRecordingDevice(
                                config_.audio.recording_endpoint_name)) {
                            moonmic::logInfo() << "[AudioReceiver] Auto-set default mic to: "
                                      << config_.audio.recording_endpoint_name << std::endl;
                        }
                    }
                }
            }
#endif

            uint32_t out_len = MAX_FRAMES;

            int err = resampler_.process(decode_buffer_, in_len, resample_buffer_, out_len);

            if (err != RESAMPLER_ERR_SUCCESS) {
                stats_.packets_dropped++;
                moonmic::logError() << "[AudioReceiver] RAW resampling failed: " << err << std::endl;
                return;
            }

            output_buffer = resample_buffer_;
            output_frames = static_cast<int>(out_len);
        }

        raw_packet_count_++;
        if (raw_packet_count_ % 100 == 1 && raw_packet_count_ > 1) {
            if (isDebugMode()) {
                moonmic::logInfo() << "[AudioReceiver] Processing RAW: packet #" << stats_.packets_received << ", "
                          << output_frames << " frames" << std::endl;
            }
        }
    } else {

        int decoded_frames =
            decoder_->decode(payload, static_cast<int>(payload_size), decode_buffer_, static_cast<int>(MAX_FRAMES));
        if (decoded_frames < 0) {
            stats_.packets_dropped++;
            moonmic::logError() << "[AudioReceiver] Decode failed for packet from " << sender_ip << std::endl;
            return;
        }

        output_frames = decoded_frames;

        if (system_sample_rate_ != detected_stream_rate_ && resampler_.isInitialized()) {
            uint32_t in_len = static_cast<uint32_t>(decoded_frames);
            uint32_t out_len = MAX_FRAMES;

            int err = resampler_.process(decode_buffer_, in_len, resample_buffer_, out_len);

            if (err != RESAMPLER_ERR_SUCCESS) {
                stats_.packets_dropped++;
                moonmic::logError() << "[AudioReceiver] Resampling failed: " << err << std::endl;
                return;
            }

            output_buffer = resample_buffer_;
            output_frames = static_cast<int>(out_len);
        }

        opus_packet_count_++;
        if (opus_packet_count_ % 100 == 1 && opus_packet_count_ > 1) {
            if (static_cast<uint32_t>(config_.audio.resampling_rate) == detected_stream_rate_) {
                if (isDebugMode()) {
                    moonmic::logInfo() << "[AudioReceiver] Processing Opus: packet #" << stats_.packets_received << ", decoded "
                              << output_frames << " frames" << std::endl;
                }
            } else {
                if (isDebugMode()) {
                    moonmic::logInfo() << "[AudioReceiver] Processing Opus: packet #" << stats_.packets_received
                              << ", decoded=" << decoded_frames << " frames @ " << detected_stream_rate_ << "Hz"
                              << ", resampled=" << output_frames << " frames @ " << config_.audio.resampling_rate
                              << "Hz (Speex)" << std::endl;
                }
            }
        }
    }

    if (!config_.audio.use_speaker_mode && config_.audio.recording_endpoint_name.find("Steam") != std::string::npos) {
        if (!steam_attenuation_logged_) {
            moonmic::logInfo()
                << "[AudioReceiver] Steam WDM-KS detected: applying 15% pre-attenuation to compensate for driver AGC"
                << std::endl;
            steam_attenuation_logged_ = true;
        }

        const float STEAM_ATTENUATION = 0.15f;
        const size_t sample_count = static_cast<size_t>(output_frames) * static_cast<size_t>(config_.audio.channels);
        for (size_t i = 0; i < sample_count; i++) {
            output_buffer[i] *= STEAM_ATTENUATION;
        }
    }

    if (!virtual_device_->write(output_buffer, output_frames, config_.audio.channels)) {
        stats_.packets_dropped++;
        moonmic::logError() << "[AudioReceiver] Failed to write audio output" << std::endl;
    }

    stats_.is_receiving = true;
}

bool AudioReceiver::validateHandshake(const uint8_t* data, size_t size, const std::string& sender_ip, uint16_t& out_w,
                                      uint16_t& out_h) {
    if (size < MOONMIC_HANDSHAKE_SIZE) {
        moonmic::logError() << "[AudioReceiver] Packet too small for handshake: " << size << " bytes" << std::endl;
        return false;
    }

    moonmic_handshake_t hs = {};
    if (!moonmic_decode_handshake_le(data, size, &hs)) {
        moonmic::logError() << "[AudioReceiver] Malformed handshake" << std::endl;
        return false;
    }

    client_devicename_.assign(hs.devicename, hs.devicename_len);
    stats_.client_name = client_devicename_;

    if (config_.security.enable_whitelist && !isClientAllowed(sender_ip)) {
        moonmic::logError() << "[AudioReceiver] DENY: " << sender_ip << " is not in the client whitelist" << std::endl;
        return false;
    }

    if (!config_.security.enable_whitelist) {
        moonmic::logInfo() << "[AudioReceiver] Client connected: " << client_devicename_ << " [whitelist disabled]" << std::endl;

        last_packet_time_ = std::chrono::steady_clock::now();
        stats_.is_connected = true;
        return true;
    }

    // Sunshine's UUID differs from the Moonlight client UUID, so pair_status is
    // the authentication signal for this handshake.
    if (hs.pair_status != 1) {
        auto now = std::chrono::steady_clock::now();
        auto grace_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_validated_time_).count();
        bool grace = (last_validated_time_.time_since_epoch().count() != 0) && (last_validated_ip_ == sender_ip) &&
                     (grace_ms < 8000);

        if (grace) {
            moonmic::logInfo() << "[AudioReceiver] Grace-accept pair_status=0 during Sunshine restart (" << grace_ms
                      << "ms since last validation)" << std::endl;
        } else {
            moonmic::logError() << "[AudioReceiver] DENY: Client '" << client_devicename_
                      << "' not validated by Sunshine (pair_status=" << (int)hs.pair_status << ")" << std::endl;
            moonmic::logError() << "[AudioReceiver] Ensure client is paired with Sunshine host" << std::endl;
            return false;
        }
    }

    moonmic::logInfo() << "[AudioReceiver] Client validated (pair_status=1): " << client_devicename_ << std::endl;

    if (hs.version >= MOONMIC_PROTOCOL_VERSION && hs.display_width > 0 && hs.display_height > 0) {
        moonmic::logInfo() << "[AudioReceiver] Client requests display resolution: " << hs.display_width << "x"
                  << hs.display_height << std::endl;

        out_w = 0;
        out_h = 0;
        if (sunshine_webui_) {
            sunshine_webui_->getCurrentResolution(out_w, out_h);
        }

        bool force_update = (hs.flags & MOONMIC_FLAG_FORCE_UPDATE) != 0;
        bool should_update = true;

        if (out_w > 0 && out_h > 0 && !force_update) {
            if (out_w != hs.display_width || out_h != hs.display_height) {
                moonmic::logInfo() << "[AudioReceiver] Resolution mismatch (Current: " << out_w << "x" << out_h
                          << ", Target: " << hs.display_width << "x" << hs.display_height
                          << "). Waiting for FORCE flag." << std::endl;
                should_update = false;
            }
        }

        bool is_valid = false;
        if (hs.display_width == 1280 && hs.display_height == 720) is_valid = true;
        if (hs.display_width == 1600 && hs.display_height == 900) is_valid = true;
        if (hs.display_width == 1920 && hs.display_height == 1080) is_valid = true;
        if (hs.display_width == 2560 && hs.display_height == 1440) is_valid = true;
        if (hs.display_width == 3840 && hs.display_height == 2160) is_valid = true;

        if (is_valid && should_update) {
            if (!applyDisplayResolution(hs.display_width, hs.display_height)) {
                moonmic::logError() << "[AudioReceiver] Warning: host resolution request could not be applied automatically"
                          << std::endl;
            }
        } else if (!is_valid) {
            moonmic::logError() << "[AudioReceiver] Invalid resolution request: " << hs.display_width << "x" << hs.display_height
                      << std::endl;
        }
    }

    return true;
}

bool AudioReceiver::applyDisplayResolution(uint16_t width, uint16_t height) {
    bool applied = false;
    if (sunshine_webui_) {

        uint16_t current_w = 0, current_h = 0;
        bool has_current = sunshine_webui_->getCurrentResolution(current_w, current_h);
        bool already_correct = has_current && (current_w == width) && (current_h == height);

        if (already_correct) {
            moonmic::logInfo() << "[AudioReceiver] Resolution already set to " << width << "x" << height
                      << " - No restart needed" << std::endl;
            return true;
        }

        if (sunshine_webui_->setDisplayResolution(width, height)) {
            moonmic::logInfo() << "[AudioReceiver] Sunshine configured for " << width << "x" << height
                      << " -> 960x544 downscale (host mode intact)" << std::endl;
            applied = true;

            if (sunshine_webui_->restartSunshine()) {
                moonmic::logInfo() << "[AudioReceiver] Sunshine restart requested after resolution change" << std::endl;
            } else {
                moonmic::logError() << "[AudioReceiver] Sunshine restart request failed" << std::endl;
            }
        } else {
            moonmic::logError() << "[AudioReceiver] Sunshine WebUI failed to apply resolution" << std::endl;
        }
    } else {
        moonmic::logError() << "[AudioReceiver] Sunshine WebUI not available - skipping API resolution change" << std::endl;
    }

    // Do not force host display changes; rely solely on Sunshine remapping.
    return applied;
}

AudioReceiver::Stats AudioReceiver::getStats() {
    std::lock_guard<std::mutex> lock(audio_mutex_);

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
                    moonmic::logInfo() << "[AudioReceiver] Client disconnected (timeout): " << client_devicename_ << std::endl;
                    resetConnectionState();
                }
            }
        }
    }
    return stats_;
}

} // namespace moonmic
