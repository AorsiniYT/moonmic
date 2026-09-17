#include "logger.h"

#include "../virtual_device.h"
#include "driver_installer.h"
#include "audio_utils.h"
#define INITGUID
#include <windows.h>
#include <vector>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>
#include <iostream>
#include <cstdint>

#ifdef USE_PORTAUDIO
#include "platform/portaudio/virtual_device_pa.h"
#endif

namespace moonmic {

class VirtualDeviceWindows : public VirtualDevice {
  public:
    VirtualDeviceWindows()
        : audio_client_(nullptr), render_client_(nullptr), buffer_frame_count_(0), system_sample_rate_(0),
          system_channels_(0), convert_to_int16_(false) {

        HRESULT hr = CoInitialize(NULL);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            moonmic::logError() << "[VirtualDevice] Failed to initialize COM: 0x" << std::hex << hr << std::endl;
        }
    }

    ~VirtualDeviceWindows() override {
        close();
        CoUninitialize();
    }

    bool init(const std::string& device_name, int, int) override {
        HRESULT hr;
        IMMDevice* device = nullptr;

        if (device_name.empty()) {

            moonmic::logInfo() << "[VirtualDevice] Using default system speakers (debug mode)" << std::endl;

            IMMDeviceEnumerator* enumerator = NULL;
            hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&enumerator);

            if (FAILED(hr)) {
                moonmic::logError() << "[VirtualDevice] Failed to create device enumerator" << std::endl;
                return false;
            }

            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
            enumerator->Release();

            if (FAILED(hr) || !device) {
                moonmic::logError() << "[VirtualDevice] Failed to get default audio device" << std::endl;
                return false;
            }

            moonmic::logInfo() << "[VirtualDevice] Initialized with default speakers" << std::endl;
        } else {

            std::string playback_device_name = device_name;

            if (device_name == "CABLE Output") {
                playback_device_name = "CABLE Input";
                moonmic::logInfo() << "[VirtualDevice] Mapped VB-Cable: " << device_name << " -> " << playback_device_name
                          << std::endl;
            }

            moonmic::logInfo() << "[VirtualDevice] Searching for playback device: " << playback_device_name << std::endl;

            device = findDeviceByName(playback_device_name);
            if (!device) {
                moonmic::logError() << "[VirtualDevice] Failed to find virtual audio device: " << playback_device_name
                          << std::endl;
                moonmic::logError() << "[VirtualDevice] Make sure the driver is installed and the device name is correct"
                          << std::endl;
                moonmic::logError() << "[VirtualDevice] Original recording endpoint: " << device_name << std::endl;
                return false;
            }

            moonmic::logInfo() << "[VirtualDevice] Successfully opened playback device: " << playback_device_name << std::endl;
            moonmic::logInfo() << "[VirtualDevice] Virtual microphone endpoint: " << device_name << std::endl;
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audio_client_);
        device->Release();

        if (FAILED(hr)) {
            moonmic::logError() << "[VirtualDevice] Failed to activate audio client" << std::endl;
            return false;
        }

        WAVEFORMATEX* mix_format = nullptr;
        hr = audio_client_->GetMixFormat(&mix_format);
        if (FAILED(hr)) {
            audio_client_->Release();
            audio_client_ = nullptr;
            return false;
        }

        WAVEFORMATEXTENSIBLE target_float = {};
        target_float.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        target_float.Format.nChannels = 2;
        target_float.Format.nSamplesPerSec = mix_format->nSamplesPerSec;
        target_float.Format.wBitsPerSample = 32;
        target_float.Format.nBlockAlign = (target_float.Format.nChannels * target_float.Format.wBitsPerSample) / 8;
        target_float.Format.nAvgBytesPerSec = target_float.Format.nSamplesPerSec * target_float.Format.nBlockAlign;
        target_float.Format.cbSize = 22;
        target_float.Samples.wValidBitsPerSample = 32;
        target_float.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        target_float.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

        WAVEFORMATEX* target_req = reinterpret_cast<WAVEFORMATEX*>(&target_float);
        WAVEFORMATEX* target_format = nullptr;
        WAVEFORMATEX* closest_match = nullptr;
        bool using_allocated_format = false;

        moonmic::logInfo() << "[VirtualDevice] Requesting 32-bit IEEE Float format..." << std::endl;
        hr = audio_client_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, target_req, &closest_match);

        if (hr == S_OK) {
            moonmic::logInfo() << "[VirtualDevice] System accepted 32-bit Float format" << std::endl;
            target_format = target_req;
        } else {
            moonmic::logError() << "[VirtualDevice] 32-bit Float rejected (hr=0x" << std::hex << hr << ")" << std::endl;

            if (hr == S_FALSE && closest_match) {
                moonmic::logInfo() << "[VirtualDevice] Using closest match suggested by system" << std::endl;
                target_format = closest_match;
                using_allocated_format = true;
            } else {
                moonmic::logInfo() << "[VirtualDevice] Falling back to system Mix Format" << std::endl;
                target_format = mix_format;
            }
        }

        system_sample_rate_ = target_format->nSamplesPerSec;
        system_channels_ = target_format->nChannels;

        if (target_format->wBitsPerSample == 16) {
            convert_to_int16_ = true;
            moonmic::logInfo() << "[VirtualDevice] Format is 16-bit Integer. Enabling Float->Int16 conversion." << std::endl;
        } else if (target_format->wBitsPerSample == 32) {

            if (target_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                convert_to_int16_ = false;
                moonmic::logInfo() << "[VirtualDevice] Format is 32-bit IEEE Float. No conversion needed." << std::endl;
            } else if (target_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                WAVEFORMATEXTENSIBLE* ext = (WAVEFORMATEXTENSIBLE*)target_format;
                if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                    convert_to_int16_ = false;
                    moonmic::logInfo() << "[VirtualDevice] Format is 32-bit IEEE Float (Extensible). No conversion needed."
                              << std::endl;
                } else {
                    convert_to_int16_ = false;
                    moonmic::logInfo()
                        << "[VirtualDevice] Format is 32-bit Extensible (SubFormat unknown). Assuming Float to be safe."
                        << std::endl;
                }
            } else {
                convert_to_int16_ = false;
                moonmic::logInfo() << "[VirtualDevice] Format is 32-bit (Tag " << target_format->wFormatTag
                          << "). Assuming Float." << std::endl;
            }
        } else {
            convert_to_int16_ = false;
            moonmic::logInfo() << "[VirtualDevice] WARNING: Unknown bit depth " << target_format->wBitsPerSample
                      << ". Assuming Float." << std::endl;
        }

        moonmic::logInfo() << "[VirtualDevice] Target format: " << system_sample_rate_ << "Hz, " << system_channels_ << "ch"
                  << std::endl;

        hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, target_format, NULL);

        if (using_allocated_format) {
            CoTaskMemFree(target_format);
        }
        CoTaskMemFree(mix_format);

        if (FAILED(hr)) {
            moonmic::logError() << "[VirtualDevice] Failed to initialize (error: 0x" << std::hex << hr << ")" << std::endl;
            audio_client_->Release();
            audio_client_ = nullptr;
            return false;
        }

        moonmic::logInfo() << "[VirtualDevice] Successfully initialized @ " << system_sample_rate_ << "Hz, " << system_channels_
                  << "ch" << std::endl;

        hr = audio_client_->GetBufferSize(&buffer_frame_count_);
        if (FAILED(hr)) {
            audio_client_->Release();
            audio_client_ = nullptr;
            return false;
        }

        hr = audio_client_->GetService(__uuidof(IAudioRenderClient), (void**)&render_client_);

        if (FAILED(hr)) {
            audio_client_->Release();
            audio_client_ = nullptr;
            return false;
        }

        hr = audio_client_->Start();
        if (FAILED(hr)) {
            render_client_->Release();
            audio_client_->Release();
            render_client_ = nullptr;
            audio_client_ = nullptr;
            return false;
        }

        return true;
    }

    bool write(const float* data, size_t frames, int channels) override {
        if (!render_client_) {
            return false;
        }

        UINT32 padding;
        HRESULT hr = audio_client_->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            return false;
        }

        UINT32 available = buffer_frame_count_ - padding;
        if (available < frames) {
            frames = available;
        }

        if (frames == 0) {
            return true;
        }

        BYTE* buffer;
        hr = render_client_->GetBuffer(frames, &buffer);
        if (FAILED(hr)) {
            return false;
        }

        float* float_buffer = reinterpret_cast<float*>(buffer);
        int16_t* int16_buffer = reinterpret_cast<int16_t*>(buffer);

        auto toInt16 = [](float sample) -> int16_t {
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            return static_cast<int16_t>(sample * 32767.0f);
        };

        if (channels == system_channels_) {
            if (convert_to_int16_) {
                for (size_t i = 0; i < frames * channels; i++) {
                    int16_buffer[i] = toInt16(data[i]);
                }
            } else {
                memcpy(buffer, data, frames * channels * sizeof(float));
            }
        } else if (channels == 1 && system_channels_ == 2) {

            for (size_t f = 0; f < frames; f++) {
                if (convert_to_int16_) {
                    int16_t sample = toInt16(data[f]);
                    int16_buffer[f * 2 + 0] = sample;
                    int16_buffer[f * 2 + 1] = sample;
                } else {
                    float_buffer[f * 2 + 0] = data[f];
                    float_buffer[f * 2 + 1] = data[f];
                }
            }
        } else if (channels == 1 && system_channels_ > 2) {

            for (size_t f = 0; f < frames; f++) {
                if (convert_to_int16_) {
                    int16_t sample = toInt16(data[f]);
                    int16_buffer[f * system_channels_ + 0] = sample;
                    int16_buffer[f * system_channels_ + 1] = sample;
                    for (int ch = 2; ch < system_channels_; ch++)
                        int16_buffer[f * system_channels_ + ch] = 0;
                } else {
                    float_buffer[f * system_channels_ + 0] = data[f];
                    float_buffer[f * system_channels_ + 1] = data[f];
                    for (int ch = 2; ch < system_channels_; ch++)
                        float_buffer[f * system_channels_ + ch] = 0.0f;
                }
            }
        } else {

            for (size_t f = 0; f < frames; f++) {
                for (int ch = 0; ch < system_channels_; ch++) {
                    if (convert_to_int16_) {
                        int16_buffer[f * system_channels_ + ch] = toInt16(data[f * channels + (ch % channels)]);
                    } else {
                        float_buffer[f * system_channels_ + ch] = data[f * channels + (ch % channels)];
                    }
                }
            }
        }

        hr = render_client_->ReleaseBuffer(frames, 0);
        return SUCCEEDED(hr);
    }

    void close() override {
        if (audio_client_) {
            audio_client_->Stop();
        }

        if (render_client_) {
            render_client_->Release();
            render_client_ = nullptr;
        }

        if (audio_client_) {
            audio_client_->Release();
            audio_client_ = nullptr;
        }
    }

    int getSampleRate() const override { return system_sample_rate_; }

  private:
    IAudioClient* audio_client_;
    IAudioRenderClient* render_client_;
    UINT32 buffer_frame_count_;
    int system_sample_rate_;
    int system_channels_;
    bool convert_to_int16_;

    IMMDevice* findDeviceByName(const std::string& target_name) {
        IMMDeviceEnumerator* enumerator = NULL;
        IMMDeviceCollection* collection = NULL;

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                      (void**)&enumerator);

        if (FAILED(hr)) {
            moonmic::logError() << "[VirtualDevice] Failed to create device enumerator" << std::endl;
            return NULL;
        }

        hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
        if (FAILED(hr)) {
            moonmic::logError() << "[VirtualDevice] Failed to enumerate audio endpoints" << std::endl;
            enumerator->Release();
            return NULL;
        }

        UINT count;
        collection->GetCount(&count);

        moonmic::logInfo() << "[VirtualDevice] Searching for device: '" << target_name << "'" << std::endl;
        moonmic::logInfo() << "[VirtualDevice] Found " << count << " render devices" << std::endl;

        IMMDevice* result = NULL;

        for (UINT i = 0; i < count; i++) {
            IMMDevice* device = NULL;
            collection->Item(i, &device);

            if (device) {
                IPropertyStore* props = NULL;
                device->OpenPropertyStore(STGM_READ, &props);

                if (props) {
                    PROPVARIANT var_name;
                    PropVariantInit(&var_name);

                    props->GetValue(PKEY_Device_FriendlyName, &var_name);

                    if (var_name.vt == VT_LPWSTR) {
                        const std::string name = platform::windows::WideToUtf8(var_name.pwszVal);

                        moonmic::logInfo() << "[VirtualDevice]   Device " << i << ": '" << name << "'" << std::endl;

                        if (name == target_name || name.find(target_name) != std::string::npos) {
                            moonmic::logInfo() << "[VirtualDevice] MATCH! Using device: '" << name << "'" << std::endl;
                            result = device;
                            result->AddRef();
                        }
                    }

                    PropVariantClear(&var_name);
                    props->Release();
                }

                device->Release();
            }

            if (result) break;
        }

        if (!result) {
            moonmic::logError() << "[VirtualDevice] No matching device found for: '" << target_name << "'" << std::endl;
        }

        collection->Release();
        enumerator->Release();

        return result;
    }
};

std::unique_ptr<VirtualDevice> VirtualDevice::create() {
#ifdef USE_PORTAUDIO

    return std::make_unique<VirtualDevicePortAudio>();
#else
    // Fallback to legacy WASAPI implementation
    return std::make_unique<VirtualDeviceWindows>();
#endif
}

} // namespace moonmic
