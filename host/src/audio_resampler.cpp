#include "audio_resampler.h"

namespace moonmic {

AudioResampler::~AudioResampler() {
    reset();
}

int AudioResampler::configure(uint32_t channels, uint32_t input_rate, uint32_t output_rate) {
    if (state_ && channels_ == channels && input_rate_ == input_rate && output_rate_ == output_rate) {
        return RESAMPLER_ERR_SUCCESS;
    }

    int error = RESAMPLER_ERR_SUCCESS;
    SpeexResamplerState* replacement = speex_resampler_init(channels, input_rate, output_rate, 10, &error);
    if (!replacement || error != RESAMPLER_ERR_SUCCESS) {
        if (replacement) {
            speex_resampler_destroy(replacement);
        }
        return error != RESAMPLER_ERR_SUCCESS ? error : RESAMPLER_ERR_ALLOC_FAILED;
    }

    reset();
    state_ = replacement;
    channels_ = channels;
    input_rate_ = input_rate;
    output_rate_ = output_rate;
    return RESAMPLER_ERR_SUCCESS;
}

void AudioResampler::reset() {
    if (state_) {
        speex_resampler_destroy(state_);
        state_ = nullptr;
    }
    channels_ = 0;
    input_rate_ = 0;
    output_rate_ = 0;
}

int AudioResampler::setRates(uint32_t input_rate, uint32_t output_rate) {
    if (!state_) {
        return RESAMPLER_ERR_BAD_STATE;
    }

    const int error = speex_resampler_set_rate(state_, input_rate, output_rate);
    if (error == RESAMPLER_ERR_SUCCESS) {
        input_rate_ = input_rate;
        output_rate_ = output_rate;
    }
    return error;
}

int AudioResampler::process(const float* input, uint32_t& input_frames, float* output, uint32_t& output_frames) {
    if (!state_) {
        return RESAMPLER_ERR_BAD_STATE;
    }

    spx_uint32_t in_length = input_frames;
    spx_uint32_t out_length = output_frames;
    const int error = channels_ == 1
                          ? speex_resampler_process_float(state_, 0, input, &in_length, output, &out_length)
                          : speex_resampler_process_interleaved_float(state_, input, &in_length, output, &out_length);
    input_frames = in_length;
    output_frames = out_length;
    return error;
}

} // namespace moonmic
