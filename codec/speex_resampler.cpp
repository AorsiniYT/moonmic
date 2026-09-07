
#include "moonmic_internal.h"
#include "moonmic_debug.h"
#include <speex/speex_resampler.h>
#include <stdlib.h>
#include <string.h>

struct moonmic_speex_resampler_t {
    SpeexResamplerState* resampler;
    uint32_t in_rate;
    uint32_t out_rate;
    uint8_t channels;
};

moonmic_speex_resampler_t* moonmic_speex_resampler_create(uint32_t in_rate, uint32_t out_rate, uint8_t channels) {
    MOONMIC_LOG("[speex_resampler] Creating: %uHz → %uHz, %dch", in_rate, out_rate, channels);

    moonmic_speex_resampler_t* res = (moonmic_speex_resampler_t*)calloc(1, sizeof(moonmic_speex_resampler_t));
    if (!res) {
        MOONMIC_LOG("[speex_resampler] ERROR: Failed to allocate resampler");
        return NULL;
    }

    int err;

    res->resampler = speex_resampler_init(
        channels,
        in_rate,
        out_rate,
        SPEEX_RESAMPLER_QUALITY_VOIP,
        &err
    );

    if (err != RESAMPLER_ERR_SUCCESS || !res->resampler) {
        MOONMIC_LOG("[speex_resampler] ERROR: speex_resampler_init failed: %d", err);
        free(res);
        return NULL;
    }

    res->in_rate = in_rate;
    res->out_rate = out_rate;
    res->channels = channels;

    MOONMIC_LOG("[speex_resampler] Created successfully");
    return res;
}

void moonmic_speex_resampler_destroy(moonmic_speex_resampler_t* resampler) {
    if (!resampler) {
        return;
    }

    if (resampler->resampler) {
        speex_resampler_destroy(resampler->resampler);
    }

    free(resampler);
}

int moonmic_speex_resampler_process(moonmic_speex_resampler_t* resampler,
                                     const int16_t* input, uint32_t in_frames,
                                     int16_t* output, uint32_t* out_frames) {
    if (!resampler || !resampler->resampler || !input || !output || !out_frames) {
        MOONMIC_LOG("[speex_resampler] ERROR: Invalid parameters");
        return -1;
    }

    spx_uint32_t in_len = in_frames;
    spx_uint32_t out_len = *out_frames;

    int result = speex_resampler_process_interleaved_int(
        resampler->resampler,
        input,
        &in_len,
        output,
        &out_len
    );

    if (result != RESAMPLER_ERR_SUCCESS) {
        MOONMIC_LOG("[speex_resampler] ERROR: process failed: %d (in=%u, out=%u)",
            result, in_len, out_len);
        return -1;
    }

    *out_frames = out_len;
    return 0;
}
