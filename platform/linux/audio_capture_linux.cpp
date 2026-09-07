
#include "moonmic_internal.h"
#include <pulse/simple.h>
#include <pulse/error.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pa_simple* pulse;
    uint32_t sample_rate;
    uint8_t channels;
} linux_audio_data_t;

static bool linux_audio_init(audio_capture_t* self, uint32_t sample_rate, uint8_t channels) {
    linux_audio_data_t* data = (linux_audio_data_t*)calloc(1, sizeof(linux_audio_data_t));
    if (!data) {
        return false;
    }

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.rate = sample_rate;
    ss.channels = channels;

    pa_buffer_attr attr;
    attr.maxlength = (uint32_t)-1;
    attr.tlength = (uint32_t)-1;
    attr.prebuf = (uint32_t)-1;
    attr.minreq = (uint32_t)-1;
    attr.fragsize = 480 * channels * sizeof(float);

    int error;

    data->pulse = pa_simple_new(NULL, "moonmic", PA_STREAM_RECORD, NULL, "Microphone Input", &ss, NULL, &attr, &error);

    if (!data->pulse) {
        free(data);
        return false;
    }

    data->sample_rate = sample_rate;
    data->channels = channels;
    self->platform_data = data;

    return true;
}

static int linux_audio_read(audio_capture_t* self, float* buffer, size_t frames) {
    linux_audio_data_t* data = (linux_audio_data_t*)self->platform_data;
    if (!data || !data->pulse) {
        return -1;
    }

    size_t bytes_to_read = frames * data->channels * sizeof(float);
    int error;

    if (pa_simple_read(data->pulse, buffer, bytes_to_read, &error) < 0) {
        return -1;
    }

    return (int)frames;
}

static void linux_audio_close(audio_capture_t* self) {
    linux_audio_data_t* data = (linux_audio_data_t*)self->platform_data;
    if (!data) {
        return;
    }

    if (data->pulse) {
        pa_simple_free(data->pulse);
    }

    free(data);
    self->platform_data = NULL;
}

static uint32_t linux_audio_get_native_sample_rate(audio_capture_t* self) {
    linux_audio_data_t* data = (linux_audio_data_t*)self->platform_data;
    return data ? data->sample_rate : 48000;
}

audio_capture_t* audio_capture_create_linux(void) {
    audio_capture_t* capture = (audio_capture_t*)calloc(1, sizeof(audio_capture_t));
    if (!capture) {
        return NULL;
    }

    capture->init = linux_audio_init;
    capture->get_native_sample_rate = linux_audio_get_native_sample_rate;
    capture->read = linux_audio_read;
    capture->close = linux_audio_close;
    capture->platform_data = NULL;

    return capture;
}
