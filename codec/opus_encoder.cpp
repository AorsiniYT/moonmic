
#include "moonmic_internal.h"
#include "moonmic_debug.h"
#include <opus/opus.h>
#include <stdlib.h>

moonmic_opus_encoder_t* moonmic_opus_encoder_create(uint32_t sample_rate, uint8_t channels, uint32_t bitrate) {
    MOONMIC_LOG("[opus_encoder] Creating encoder: %uHz, %dch, %ubps", sample_rate, channels, bitrate);

    moonmic_opus_encoder_t* enc = (moonmic_opus_encoder_t*)calloc(1, sizeof(moonmic_opus_encoder_t));
    if (!enc) {
        MOONMIC_LOG("[opus_encoder] ERROR: Failed to allocate encoder");
        return NULL;
    }

    int error;

    enc->encoder = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_AUDIO, &error);

    if (error != OPUS_OK || !enc->encoder) {
        MOONMIC_LOG("[opus_encoder] ERROR: opus_encoder_create failed: %d", error);
        free(enc);
        return NULL;
    }

    opus_encoder_ctl((OpusEncoder*)enc->encoder, OPUS_SET_BITRATE(bitrate));

    opus_encoder_ctl((OpusEncoder*)enc->encoder, OPUS_SET_COMPLEXITY(10));

    opus_encoder_ctl((OpusEncoder*)enc->encoder, OPUS_SET_VBR(1));

    opus_encoder_ctl((OpusEncoder*)enc->encoder, OPUS_SET_DTX(0));

    MOONMIC_LOG("[opus_encoder] Created: %dHz, %dch, %dbps (AUDIO mode, complexity=10, VBR)", sample_rate, channels,
                bitrate);

    enc->sample_rate = sample_rate;
    enc->channels = channels;
    enc->bitrate = bitrate;

    MOONMIC_LOG("[opus_encoder] Encoder created successfully");
    return enc;
}

void moonmic_opus_encoder_destroy(moonmic_opus_encoder_t* encoder) {
    if (!encoder) {
        return;
    }

    if (encoder->encoder) {
        opus_encoder_destroy((OpusEncoder*)encoder->encoder);
    }

    free(encoder);
}

int moonmic_opus_encoder_encode(moonmic_opus_encoder_t* encoder, const float* pcm, int frame_size, uint8_t* output,
                                int max_output_bytes) {
    if (!encoder || !encoder->encoder || !pcm || !output) {
        MOONMIC_LOG("[opus_encoder] ERROR: Invalid parameters");
        return -1;
    }

    static int encode_count = 0;
    if (encode_count < 5) {
        MOONMIC_LOG("[opus_encoder] Encoding frame #%d: %d samples, max_out=%d bytes", encode_count, frame_size,
                    max_output_bytes);
        MOONMIC_LOG("[opus_encoder] Input PCM samples[0-4]: %.4f %.4f %.4f %.4f %.4f", pcm[0], pcm[1], pcm[2], pcm[3],
                    pcm[4]);
        encode_count++;
    }

    int result = opus_encode_float((OpusEncoder*)encoder->encoder, pcm, frame_size, output, max_output_bytes);

    if (result < 0) {
        MOONMIC_LOG("[opus_encoder] ERROR: opus_encode_float failed: %d (frame_size=%d, max_out=%d)", result,
                    frame_size, max_output_bytes);
    } else if (encode_count <= 5) {
        MOONMIC_LOG("[opus_encoder] SUCCESS: Encoded %d bytes (frame_size=%d)", result, frame_size);
    }

    return result;
}
