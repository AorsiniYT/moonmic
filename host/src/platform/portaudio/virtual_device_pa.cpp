#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <pa_win_wasapi.h>

#ifndef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b,
            0x71);
#endif

#endif

#include "virtual_device_pa.h"
#include <iostream>
#include <cstring>
#include <vector>

namespace moonmic {

VirtualDevicePortAudio::VirtualDevicePortAudio() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "[PortAudio] Initialize failed: " << Pa_GetErrorText(err) << std::endl;
    } else {
        std::cout << "[PortAudio] Library initialized." << std::endl;
    }
}

VirtualDevicePortAudio::~VirtualDevicePortAudio() {
    close();
    Pa_Terminate();
}

void VirtualDevicePortAudio::close() {
    if (stream_) {
        Pa_AbortStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }

    if (resampler_) {
        speex_resampler_destroy(resampler_);
        resampler_ = nullptr;
    }
}

bool VirtualDevicePortAudio::init(const std::string& device_name, int sample_rate, int channels) {
    if (stream_) close();

    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        std::cerr << "[PortAudio] No devices found." << std::endl;
        return false;
    }

    int outputDeviceIndex = -1;
    bool found_ks = false;

    std::cout << "[PortAudio] Searching for device containing: '" << device_name << "'" << std::endl;

    if (device_name.empty()) {
        outputDeviceIndex = Pa_GetDefaultOutputDevice();
        std::cout << "[PortAudio] No device name specified. Using system default output." << std::endl;
    } else {
        for (int i = 0; i < numDevices; i++) {
            const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
            const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);

            if (deviceInfo->maxOutputChannels < channels) continue;

            std::string name = deviceInfo->name;
            std::string apiName = hostApiInfo->name;

            bool name_match = false;

            name_match = (name.find(device_name) != std::string::npos);

            if (!name_match && device_name.find("Steam") != std::string::npos) {
                if (name.find("Steam") != std::string::npos && name.find("Microphone") != std::string::npos) {

                    name_match = true;
                    std::cout << "[PortAudio] Matched Steam Microphone output endpoint: " << name << std::endl;
                }
            }

            if (!name_match && device_name.find("CABLE Output") != std::string::npos) {
                if (name.find("CABLE Input") != std::string::npos ||
                    name.find("Input (VB-Audio") != std::string::npos) {
                    name_match = true;
                    std::cout << "[PortAudio] Matched VB-Cable playback endpoint: " << name << std::endl;
                }
            }

            if (name_match) {
                std::cout << "  [" << i << "] Found: " << name << " (" << apiName << ")" << std::endl;

                // WDM-KS requires high sample rates for Steam (96000Hz/88200Hz)

                bool is_ks = (apiName.find("WDM-KS") != std::string::npos);
                bool is_wasapi = (apiName.find("WASAPI") != std::string::npos);

                if (is_ks) {
                    outputDeviceIndex = i;
                    found_ks = true;
                    std::cout << "      >>> Selected as best candidate (WDM-KS)!" << std::endl;
                    break;
                } else if (is_wasapi && !found_ks) {
                    outputDeviceIndex = i;
                } else if (outputDeviceIndex == -1) {
                    outputDeviceIndex = i; // First match fallback
                }
            }
        }
    }

    if (outputDeviceIndex == -1) {
        // Fallback to default output ONLY if device_name was empty (Speaker Mode)

        if (device_name.empty()) {
            outputDeviceIndex = Pa_GetDefaultOutputDevice();
            std::cout << "[PortAudio] No device name specified. Using system default output." << std::endl;
        } else {
            std::cerr << "[PortAudio] Could not find output device matching: " << device_name << std::endl;
            return false;
        }
    }

    const PaDeviceInfo* finalDeviceInfo = Pa_GetDeviceInfo(outputDeviceIndex);
    std::cout << "[PortAudio] Opening stream on device: " << finalDeviceInfo->name << " ("
              << Pa_GetHostApiInfo(finalDeviceInfo->hostApi)->name << ")" << std::endl;

    const PaHostApiInfo* selectedHostApi = Pa_GetHostApiInfo(finalDeviceInfo->hostApi);
    bool using_wdmks = (std::string(selectedHostApi->name).find("WDM-KS") != std::string::npos);

    // WDM-KS often requires minimum 2 channels even for mono source
    int output_channels = channels;
    if (using_wdmks && channels == 1) {
        output_channels = 2;
        std::cout << "[PortAudio] WDM-KS detected: converting mono to stereo (2ch)" << std::endl;
    }

    channels_ = output_channels;

    PaStreamParameters outputParameters;
    outputParameters.device = outputDeviceIndex;
    outputParameters.channelCount = output_channels;
    outputParameters.sampleFormat = paInt16;

    if (using_wdmks) {
        outputParameters.suggestedLatency = finalDeviceInfo->defaultHighOutputLatency;
        std::cout << "[PortAudio] WDM-KS: Using High Output Latency: " << outputParameters.suggestedLatency << "s"
                  << std::endl;
    } else {
        outputParameters.suggestedLatency = finalDeviceInfo->defaultLowOutputLatency;
    }

    outputParameters.hostApiSpecificStreamInfo = NULL;

    int target_rate = (int)finalDeviceInfo->defaultSampleRate;
    int target_channels = output_channels;
    PaSampleFormat target_format = paInt16;
    is_float_ = false;

#ifdef _WIN32

    const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(finalDeviceInfo->hostApi);
    if (hostApiInfo && hostApiInfo->type == paWASAPI) {
        WAVEFORMATEXTENSIBLE wfx;
        int bytes = PaWasapi_GetDeviceDefaultFormat(&wfx, sizeof(wfx), outputDeviceIndex);
        if (bytes > 0) {
            target_rate = wfx.Format.nSamplesPerSec;
            target_channels = wfx.Format.nChannels;

            if (wfx.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                if (wfx.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                    target_format = paFloat32;
                    is_float_ = true;
                }
            } else if (wfx.Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                target_format = paFloat32;
                is_float_ = true;
            }

            std::cout << "[PortAudio] Precise WASAPI format: " << target_rate << "Hz, " << target_channels << "ch, "
                      << (is_float_ ? "Float32" : "Int16") << std::endl;
        }
    }
#endif

    source_sample_rate_ = sample_rate;
    if (source_sample_rate_ <= 0) source_sample_rate_ = target_rate;

    outputParameters.sampleFormat = target_format;
    outputParameters.channelCount = target_channels;
    channels_ = target_channels;

    rb_size_ = target_channels * target_rate * 0.8;
    ring_buffer_.resize(rb_size_, 0.0f);
    rb_read_pos_ = 0;
    rb_write_pos_ = 0;

    // WDM-KS often requires explicit buffer size

    unsigned long frames_per_buffer = using_wdmks ? 1024 : paFramesPerBufferUnspecified;
    if (using_wdmks) {
        std::cout << "[PortAudio] WDM-KS: using explicit buffer size (1024 frames)" << std::endl;
    }

    PaError err = Pa_OpenStream(&stream_, NULL, &outputParameters, (double)target_rate, frames_per_buffer, paNoFlag,
                                paCallback, this);

    if (err == paNoError) {
        actual_sample_rate_ = target_rate;
    } else {
        std::cerr << "[PortAudio] Fatal: Could not open stream with precise format: " << Pa_GetErrorText(err)
                  << std::endl;
        stream_ = nullptr;
        return false;
    }

    if (actual_sample_rate_ != source_sample_rate_) {
        std::cout << "[PortAudio] Resampling required: " << source_sample_rate_ << "Hz -> " << actual_sample_rate_
                  << "Hz" << std::endl;
        int resampler_err;
        resampler_ = speex_resampler_init(target_channels, source_sample_rate_, actual_sample_rate_, 3, &resampler_err);
        if (!resampler_) {
            std::cerr << "[PortAudio] Failed to initialize Speex resampler: " << resampler_err << std::endl;
        }
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "[PortAudio] StartStream failed: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream_);
        stream_ = nullptr;
        return false;
    }

    std::cout << "[PortAudio] Stream started successfully @ " << actual_sample_rate_ << "Hz (" << target_channels
              << "ch)" << std::endl;
    return true;
}
float VirtualDevicePortAudio::getBufferUsage() const {
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t r = rb_read_pos_;
    const size_t w = rb_write_pos_;
    const size_t s = rb_size_;

    if (s == 0) return 0.0f;

    size_t count = (w + s - r) % s;
    return (float)count / (float)s;
}

bool VirtualDevicePortAudio::write(const float* data, size_t frames, int channels) {
    if (!stream_) return false;

    const float* write_ptr = data;
    std::vector<float> mono_to_stereo_buffer;
    size_t in_frames = frames;
    int in_channels = channels;

    if (in_channels != channels_) {
        mono_to_stereo_buffer.resize(frames * channels_);
        for (size_t i = 0; i < frames; i++) {
            for (int ch = 0; ch < channels_; ch++) {

                if (in_channels == 1) {
                    mono_to_stereo_buffer[i * channels_ + ch] = data[i];
                } else if (in_channels == 2) {
                    mono_to_stereo_buffer[i * channels_ + ch] = data[i * 2 + (ch % 2)];
                } else {
                    // Fallback for other cases
                    mono_to_stereo_buffer[i * channels_ + ch] = (ch < in_channels) ? data[i * in_channels + ch] : 0.0f;
                }
            }
        }
        write_ptr = mono_to_stereo_buffer.data();
        in_channels = channels_;
    }

    std::vector<float> resampled_buffer;
    if (resampler_) {

        // Actual output might be slightly more/less due to fractional ratio
        spx_uint32_t out_frames = (frames * actual_sample_rate_ / source_sample_rate_) + 10;
        resampled_buffer.resize(out_frames * channels_);

        spx_uint32_t in_len = (spx_uint32_t)frames;
        spx_uint32_t out_len = (spx_uint32_t)out_frames;

        speex_resampler_process_interleaved_float(resampler_, write_ptr, &in_len, resampled_buffer.data(), &out_len);

        write_ptr = resampled_buffer.data();
        in_frames = out_len;
    }

    size_t samples_to_write = in_frames * channels_;

    std::lock_guard<std::mutex> lock(mutex_);

    size_t write_pos = rb_write_pos_;
    size_t read_pos = rb_read_pos_;
    size_t size = rb_size_;

    for (size_t i = 0; i < samples_to_write; i++) {
        size_t next_write_pos = (write_pos + 1) % size;
        if (next_write_pos == read_pos) {

            break;
        }
        ring_buffer_[write_pos] = write_ptr[i];
        write_pos = next_write_pos;
    }

    rb_write_pos_ = write_pos;
    return true;
}

int VirtualDevicePortAudio::paCallback(const void*, void* outputBuffer, unsigned long framesPerBuffer,
                                       const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData) {
    auto* device = static_cast<VirtualDevicePortAudio*>(userData);
    size_t samples_needed = framesPerBuffer * device->channels_;

    std::lock_guard<std::mutex> lock(device->mutex_);

    size_t read_pos = device->rb_read_pos_;
    size_t write_pos = device->rb_write_pos_;
    size_t size = device->rb_size_;

    size_t samples_read = 0;

    if (device->is_float_) {
        float* out = static_cast<float*>(outputBuffer);
        while (samples_read < samples_needed) {
            if (read_pos == write_pos) break;
            out[samples_read++] = device->ring_buffer_[read_pos];
            read_pos = (read_pos + 1) % size;
        }
        if (samples_read < samples_needed) {
            std::memset(out + samples_read, 0, (samples_needed - samples_read) * sizeof(float));
        }
    } else {
        int16_t* out = static_cast<int16_t*>(outputBuffer);
        while (samples_read < samples_needed) {
            if (read_pos == write_pos) break;
            float val = device->ring_buffer_[read_pos];
            if (val > 1.0f) val = 1.0f;
            if (val < -1.0f) val = -1.0f;
            out[samples_read++] = static_cast<int16_t>(val * 32767.0f);
            read_pos = (read_pos + 1) % size;
        }
        if (samples_read < samples_needed) {
            std::memset(out + samples_read, 0, (samples_needed - samples_read) * sizeof(int16_t));
        }
    }

    device->rb_read_pos_ = read_pos;
    return paContinue;
}

} // namespace moonmic
