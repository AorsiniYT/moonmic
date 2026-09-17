#include "logger.h"

#include <initguid.h>
#include "audio_device_manager.h"
#include <iostream>
#include <propidl.h>
#include <comdef.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

namespace {
constexpr GUID kKsDataFormatSubtypeIeeeFloat = {STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT};
}

namespace moonmic {

AudioDeviceManager::AudioDeviceManager() {
    initializeCOM();
}

AudioDeviceManager::~AudioDeviceManager() {
    cleanupCOM();
}

bool AudioDeviceManager::initializeCOM() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        moonmic::logError() << "[AudioDeviceManager] CoInitialize failed" << std::endl;
        return false;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void**)&enumerator_);
    if (FAILED(hr)) {
        moonmic::logError() << "[AudioDeviceManager] Failed to create device enumerator" << std::endl;
        return false;
    }

    hr = CoCreateInstance(CLSID_CPolicyConfigClient, NULL, CLSCTX_ALL, IID_IPolicyConfig, (void**)&policy_config_);
    if (FAILED(hr)) {
        moonmic::logError() << "[AudioDeviceManager] Failed to create IPolicyConfig (Windows 10+ required)" << std::endl;
    }

    return true;
}

void AudioDeviceManager::cleanupCOM() {
    if (policy_config_) {
        policy_config_->Release();
        policy_config_ = nullptr;
    }
    if (enumerator_) {
        enumerator_->Release();
        enumerator_ = nullptr;
    }
    CoUninitialize();
}

std::vector<AudioDeviceInfo> AudioDeviceManager::enumerateRecordingDevices() {
    std::vector<AudioDeviceInfo> devices;

    if (!enumerator_) {
        moonmic::logError() << "[AudioDeviceManager] Enumerator not initialized" << std::endl;
        return devices;
    }

    IMMDevice* defaultDevice = nullptr;
    std::wstring defaultId;
    HRESULT hr = enumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &defaultDevice);
    if (SUCCEEDED(hr)) {
        LPWSTR pwszID = NULL;
        defaultDevice->GetId(&pwszID);
        if (pwszID) {
            defaultId = pwszID;
            CoTaskMemFree(pwszID);
        }
        defaultDevice->Release();
    }

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator_->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        moonmic::logError() << "[AudioDeviceManager] EnumAudioEndpoints failed" << std::endl;
        return devices;
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; i++) {
        IMMDevice* device = nullptr;
        hr = collection->Item(i, &device);
        if (FAILED(hr)) continue;

        AudioDeviceInfo info;

        LPWSTR pwszID = NULL;
        device->GetId(&pwszID);
        if (pwszID) {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, pwszID, -1, NULL, 0, NULL, NULL);
            std::string id(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, pwszID, -1, &id[0], size_needed, NULL, NULL);
            info.id = id.c_str();
            info.is_default = (defaultId == pwszID);
            CoTaskMemFree(pwszID);
        }

        IPropertyStore* props = nullptr;
        device->OpenPropertyStore(STGM_READ, &props);
        if (props) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            props->GetValue(PKEY_Device_FriendlyName, &varName);
            if (varName.vt == VT_LPWSTR) {
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, NULL, 0, NULL, NULL);
                std::string name(size_needed, 0);
                WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, &name[0], size_needed, NULL, NULL);
                info.name = name.c_str();
                info.is_virtual = isVirtualMicrophone(info.name);
            }
            PropVariantClear(&varName);
            props->Release();
        }

        device->Release();
        devices.push_back(info);
    }

    collection->Release();
    return devices;
}

AudioDeviceInfo AudioDeviceManager::getCurrentDefaultRecordingDevice() {
    AudioDeviceInfo info;

    if (!enumerator_) return info;

    IMMDevice* device = nullptr;
    HRESULT hr = enumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    if (FAILED(hr)) return info;

    LPWSTR pwszID = NULL;
    device->GetId(&pwszID);
    if (pwszID) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, pwszID, -1, NULL, 0, NULL, NULL);
        std::string id(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, pwszID, -1, &id[0], size_needed, NULL, NULL);
        info.id = id.c_str();
        CoTaskMemFree(pwszID);
    }

    IPropertyStore* props = nullptr;
    device->OpenPropertyStore(STGM_READ, &props);
    if (props) {
        PROPVARIANT varName;
        PropVariantInit(&varName);
        props->GetValue(PKEY_Device_FriendlyName, &varName);
        if (varName.vt == VT_LPWSTR) {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, NULL, 0, NULL, NULL);
            std::string name(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, &name[0], size_needed, NULL, NULL);
            info.name = name.c_str();
            info.is_virtual = isVirtualMicrophone(info.name);
        }
        PropVariantClear(&varName);
        props->Release();
    }

    info.is_default = true;
    device->Release();
    return info;
}

bool AudioDeviceManager::setDefaultRecordingDevice(const std::string& device_id) {
    if (!policy_config_) {
        moonmic::logError() << "[AudioDeviceManager] IPolicyConfig not available" << std::endl;
        return false;
    }

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, device_id.c_str(), -1, NULL, 0);
    std::wstring wid(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, device_id.c_str(), -1, &wid[0], size_needed);

    HRESULT hr = policy_config_->SetDefaultEndpoint(wid.c_str(), eConsole);
    if (FAILED(hr)) {
        moonmic::logError() << "[AudioDeviceManager] SetDefaultEndpoint failed: " << hr << std::endl;
        return false;
    }

    policy_config_->SetDefaultEndpoint(wid.c_str(), eMultimedia);
    policy_config_->SetDefaultEndpoint(wid.c_str(), eCommunications);

    moonmic::logInfo() << "[AudioDeviceManager] Changed default recording device" << std::endl;
    return true;
}

bool AudioDeviceManager::isVirtualMicrophone(const std::string& device_name) {

    return (device_name.find("Steam") != std::string::npos && device_name.find("Microphone") != std::string::npos) ||
           (device_name.find("CABLE Output") != std::string::npos) ||
           (device_name.find("VB-Audio") != std::string::npos && device_name.find("Output") != std::string::npos);
}

int AudioDeviceManager::getNativeSampleRate(const std::string& device_name, bool is_capture) {
    NativeAudioFormat format;
    if (getNativeFormat(device_name, is_capture, format)) {
        return format.sample_rate;
    }
    return 0;
}

bool AudioDeviceManager::getNativeFormat(const std::string& device_name, bool is_capture,
                                         NativeAudioFormat& out_format) {
    if (!enumerator_) return false;

    EDataFlow dataFlow = is_capture ? eCapture : eRender;
    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = enumerator_->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) return false;

    UINT count = 0;
    collection->GetCount(&count);
    bool found = false;

    for (UINT i = 0; i < count; i++) {
        IMMDevice* device = nullptr;
        hr = collection->Item(i, &device);
        if (FAILED(hr)) continue;

        IPropertyStore* props = nullptr;
        device->OpenPropertyStore(STGM_READ, &props);
        if (props) {
            bool matched = false;

            LPWSTR pwszID = NULL;
            device->GetId(&pwszID);
            if (pwszID) {
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, pwszID, -1, NULL, 0, NULL, NULL);
                std::string id(size_needed, 0);
                WideCharToMultiByte(CP_UTF8, 0, pwszID, -1, &id[0], size_needed, NULL, NULL);
                id = id.c_str();
                if (id == device_name) matched = true;
                CoTaskMemFree(pwszID);
            }

            if (!matched) {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                props->GetValue(PKEY_Device_FriendlyName, &varName);
                if (varName.vt == VT_LPWSTR) {
                    std::wstring wname = varName.pwszVal;
                    int size_needed = MultiByteToWideChar(CP_UTF8, 0, device_name.c_str(), -1, NULL, 0);
                    std::wstring wsearch(size_needed, 0);
                    MultiByteToWideChar(CP_UTF8, 0, device_name.c_str(), -1, &wsearch[0], size_needed);

                    if (wname.find(wsearch.c_str()) != std::wstring::npos || device_name.empty()) {
                        matched = true;
                    }
                }
                PropVariantClear(&varName);
            }

            if (matched) {
                PROPVARIANT varFormat;
                PropVariantInit(&varFormat);
                hr = props->GetValue(PKEY_AudioEngine_DeviceFormat, &varFormat);
                if (SUCCEEDED(hr) && varFormat.vt == VT_BLOB) {
                    if (varFormat.blob.cbSize >= sizeof(WAVEFORMATEX)) {
                        WAVEFORMATEX* pwfx = reinterpret_cast<WAVEFORMATEX*>(varFormat.blob.pBlobData);
                        out_format.sample_rate = pwfx->nSamplesPerSec;
                        out_format.channels = pwfx->nChannels;
                        out_format.bits_per_sample = pwfx->wBitsPerSample;

                        if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                            WAVEFORMATEXTENSIBLE* pwfxe = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
                            if (pwfxe->SubFormat == kKsDataFormatSubtypeIeeeFloat) {
                                out_format.is_float = true;
                            }
                        } else if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                            out_format.is_float = true;
                        }

                        found = true;
                        moonmic::logInfo() << "[AudioDeviceManager] Native format for '" << device_name
                                  << "': " << out_format.sample_rate << "Hz, " << out_format.channels << "ch, "
                                  << out_format.bits_per_sample << "-bit "
                                  << (out_format.is_float ? "(float)" : "(int)") << std::endl;
                    }
                }
                PropVariantClear(&varFormat);
            }
            props->Release();
        }

        device->Release();
        if (found) break;
    }

    collection->Release();
    return found;
}

} // namespace moonmic
