#pragma once

#include "platform/virtual_device.h"
#include <portaudio.h>
#include <vector>
#include <mutex>
#include <speex/speex_resampler.h>

namespace moonmic {

class VirtualDevicePortAudio : public VirtualDevice {
  public:
    VirtualDevicePortAudio();
    ~VirtualDevicePortAudio() override;

    bool init(const std::string& device_name, int sample_rate, int channels) override;
    bool write(const float* data, size_t frames, int channels) override;
    void close() override;
    int getSampleRate() const override { return actual_sample_rate_; }
    float getBufferUsage() const override;

    static int paCallback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData);

  private:
    PaStream* stream_ = nullptr;
    int actual_sample_rate_ = 48000;
    int channels_ = 2;
    bool is_float_ = false;

    std::vector<float> ring_buffer_;
    size_t rb_read_pos_ = 0;
    size_t rb_write_pos_ = 0;
    size_t rb_size_ = 0;

    mutable std::mutex mutex_;

    SpeexResamplerState* resampler_ = nullptr;
    int source_sample_rate_ = 0;
};

} // namespace moonmic
