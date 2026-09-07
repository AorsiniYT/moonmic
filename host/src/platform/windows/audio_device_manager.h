
#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

namespace moonmic {

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    bool is_default;
    bool is_virtual;
};

struct NativeAudioFormat {
    int sample_rate = 0;
    int channels = 0;
    int bits_per_sample = 0;
    bool is_float = false;
};

interface IPolicyConfig : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, REFPROPERTYKEY, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, REFPROPERTYKEY, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

static const IID IID_IPolicyConfig =
    {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};

static const CLSID CLSID_CPolicyConfigClient =
    {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

class AudioDeviceManager {
public:
    AudioDeviceManager();
    ~AudioDeviceManager();

    std::vector<AudioDeviceInfo> enumerateRecordingDevices();

    AudioDeviceInfo getCurrentDefaultRecordingDevice();

    bool setDefaultRecordingDevice(const std::string& device_id);

    static bool isVirtualMicrophone(const std::string& device_name);

    bool getNativeFormat(const std::string& device_name, bool is_capture, NativeAudioFormat& out_format);

    int getNativeSampleRate(const std::string& device_name, bool is_capture);

private:
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IPolicyConfig* policy_config_ = nullptr;

    bool initializeCOM();
    void cleanupCOM();
};

}
