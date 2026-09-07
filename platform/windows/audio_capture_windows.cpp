
#include "moonmic_internal.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    IMMDeviceEnumerator* device_enum;
    IMMDevice* device;
    IAudioClient* audio_client;
    IAudioCaptureClient* capture_client;
    HANDLE audio_event;
    WAVEFORMATEX* wave_format;
    uint32_t buffer_frame_count;
    uint8_t channels;
} windows_audio_data_t;

static bool windows_audio_init(audio_capture_t* self, uint32_t sample_rate, uint8_t channels) {
    windows_audio_data_t* data = (windows_audio_data_t*)calloc(1, sizeof(windows_audio_data_t));
    if (!data) {
        return false;
    }

    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator,
                          (void**)&data->device_enum);

    if (FAILED(hr)) {
        free(data);
        return false;
    }

    hr = data->device_enum->GetDefaultAudioEndpoint(eCapture, eConsole, &data->device);

    if (FAILED(hr)) {
        data->device_enum->Release();
        free(data);
        return false;
    }

    hr = data->device->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&data->audio_client);

    if (FAILED(hr)) {
        data->device->Release();
        data->device_enum->Release();
        free(data);
        return false;
    }

    hr = data->audio_client->GetMixFormat(&data->wave_format);
    if (FAILED(hr)) {
        data->audio_client->Release();
        data->device->Release();
        data->device_enum->Release();
        free(data);
        return false;
    }

    WAVEFORMATEXTENSIBLE* wfex = (WAVEFORMATEXTENSIBLE*)data->wave_format;
    wfex->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfex->Format.nChannels = channels;
    wfex->Format.nSamplesPerSec = sample_rate;
    wfex->Format.wBitsPerSample = 32;
    wfex->Format.nBlockAlign = (wfex->Format.nChannels * wfex->Format.wBitsPerSample) / 8;
    wfex->Format.nAvgBytesPerSec = wfex->Format.nSamplesPerSec * wfex->Format.nBlockAlign;
    wfex->Format.cbSize = 22;
    wfex->Samples.wValidBitsPerSample = 32;
    wfex->dwChannelMask = (channels == 1) ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    wfex->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    data->audio_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!data->audio_event) {
        CoTaskMemFree(data->wave_format);
        data->audio_client->Release();
        data->device->Release();
        data->device_enum->Release();
        free(data);
        return false;
    }

    hr = data->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 10000000, 0,
                                        data->wave_format, NULL);

    if (FAILED(hr)) {
        CloseHandle(data->audio_event);
        CoTaskMemFree(data->wave_format);
        data->audio_client->Release();
        data->device->Release();
        data->device_enum->Release();
        free(data);
        return false;
    }

    hr = data->audio_client->SetEventHandle(data->audio_event);
    if (FAILED(hr)) {
        CloseHandle(data->audio_event);
        CoTaskMemFree(data->wave_format);
        data->audio_client->Release();
        data->device->Release();
        data->device_enum->Release();
        free(data);
        return false;
    }

    hr = data->audio_client->GetBufferSize(&data->buffer_frame_count);
    if (FAILED(hr)) {
        CloseHandle(data->audio_event);
        CoTaskMemFree(data->wave_format);
        data->audio_client->Release();
        data->device->Release();
        data->device_enum->Release();
        free(data);
        return false;
    }

    hr = data->audio_client->GetService(IID_IAudioCaptureClient, (void**)&data->capture_client);

    if (FAILED(hr)) {
        CloseHandle(data->audio_event);
        CoTaskMemFree(data->wave_format);
        data->audio_client->Release();
        data->device->Release();
        data->device_enum->Release();
        free(data);
        return false;
    }

    hr = data->audio_client->Start();
    if (FAILED(hr)) {
        data->capture_client->Release();
        CloseHandle(data->audio_event);
        CoTaskMemFree(data->wave_format);
        data->audio_client->Release();
        data->device->Release();
        data->device_enum->Release();
        free(data);
        return false;
    }

    data->channels = channels;
    self->platform_data = data;
    return true;
}

static int windows_audio_read(audio_capture_t* self, float* buffer, size_t frames) {
    windows_audio_data_t* data = (windows_audio_data_t*)self->platform_data;
    if (!data) {
        return -1;
    }

    DWORD wait_result = WaitForSingleObject(data->audio_event, 100);
    if (wait_result != WAIT_OBJECT_0) {
        return 0;
    }

    BYTE* packet_data;
    UINT32 packet_frames;
    DWORD flags;

    HRESULT hr = data->capture_client->GetBuffer(&packet_data, &packet_frames, &flags, NULL, NULL);

    if (FAILED(hr)) {
        return -1;
    }

    if (packet_frames == 0) {
        data->capture_client->ReleaseBuffer(0);
        return 0;
    }

    size_t samples_to_copy = (packet_frames < frames) ? packet_frames : frames;
    size_t bytes_to_copy = samples_to_copy * data->channels * sizeof(float);

    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        memset(buffer, 0, bytes_to_copy);
    } else {
        memcpy(buffer, packet_data, bytes_to_copy);
    }

    data->capture_client->ReleaseBuffer(packet_frames);

    return (int)samples_to_copy;
}

static void windows_audio_close(audio_capture_t* self) {
    windows_audio_data_t* data = (windows_audio_data_t*)self->platform_data;
    if (!data) {
        return;
    }

    if (data->audio_client) {
        data->audio_client->Stop();
    }

    if (data->capture_client) {
        data->capture_client->Release();
    }
    if (data->audio_client) {
        data->audio_client->Release();
    }
    if (data->wave_format) {
        CoTaskMemFree(data->wave_format);
    }
    if (data->audio_event) {
        CloseHandle(data->audio_event);
    }
    if (data->device) {
        data->device->Release();
    }
    if (data->device_enum) {
        data->device_enum->Release();
    }

    CoUninitialize();
    free(data);
    self->platform_data = NULL;
}

static uint32_t windows_audio_get_native_sample_rate(audio_capture_t* self) {
    windows_audio_data_t* data = (windows_audio_data_t*)self->platform_data;
    return data && data->wave_format ? data->wave_format->nSamplesPerSec : 48000;
}

audio_capture_t* audio_capture_create_windows(void) {
    audio_capture_t* capture = (audio_capture_t*)calloc(1, sizeof(audio_capture_t));
    if (!capture) {
        return NULL;
    }

    capture->init = windows_audio_init;
    capture->get_native_sample_rate = windows_audio_get_native_sample_rate;
    capture->read = windows_audio_read;
    capture->close = windows_audio_close;
    capture->platform_data = NULL;

    return capture;
}
