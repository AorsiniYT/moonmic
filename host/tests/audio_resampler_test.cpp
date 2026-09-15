#include "audio_resampler.h"

#include <array>

int main() {
    moonmic::AudioResampler resampler;
    if (resampler.configure(1, 48000, 48000) != RESAMPLER_ERR_SUCCESS || resampler.inputRate() != 48000 ||
        resampler.outputRate() != 48000) {
        return 1;
    }

    if (resampler.configure(1, 16000, 48000) != RESAMPLER_ERR_SUCCESS || resampler.inputRate() != 16000 ||
        resampler.outputRate() != 48000) {
        return 2;
    }

    std::array<float, 160> input{};
    std::array<float, 512> output{};
    uint32_t input_frames = static_cast<uint32_t>(input.size());
    uint32_t output_frames = static_cast<uint32_t>(output.size());
    if (resampler.process(input.data(), input_frames, output.data(), output_frames) != RESAMPLER_ERR_SUCCESS ||
        output_frames == 0) {
        return 3;
    }

    resampler.reset();
    return resampler.isInitialized() ? 4 : 0;
}
