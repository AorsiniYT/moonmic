#pragma once

#include <speex/speex_resampler.h>

#include <cstdint>

namespace moonmic {

class AudioResampler {
  public:
    AudioResampler() = default;
    ~AudioResampler();

    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    int configure(uint32_t channels, uint32_t input_rate, uint32_t output_rate);
    void reset();

    bool isInitialized() const { return state_ != nullptr; }
    uint32_t inputRate() const { return input_rate_; }
    uint32_t outputRate() const { return output_rate_; }

    int setRates(uint32_t input_rate, uint32_t output_rate);
    int process(const float* input, uint32_t& input_frames, float* output, uint32_t& output_frames);

  private:
    SpeexResamplerState* state_ = nullptr;
    uint32_t channels_ = 0;
    uint32_t input_rate_ = 0;
    uint32_t output_rate_ = 0;
};

} // namespace moonmic
