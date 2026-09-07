
#include "../virtual_device.h"
#include <pulse/simple.h>
#include <pulse/error.h>
#include <iostream>
#include <cstring>

namespace moonmic {

class VirtualDeviceLinux : public VirtualDevice {
  public:
    VirtualDeviceLinux() : pulse_(nullptr), sample_rate_(0) {}

    ~VirtualDeviceLinux() override { close(); }

    bool init(const std::string& device_name, int sample_rate, int channels) override {

        if (sample_rate <= 0) {
            sample_rate = 48000;
        }

        sample_rate_ = sample_rate;

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
        pulse_ = pa_simple_new(NULL, "moonmic-host", PA_STREAM_PLAYBACK, NULL, device_name.c_str(), &ss, NULL, &attr,
                               &error);

        if (!pulse_) {
            std::cerr << "[VirtualDevice] PulseAudio error: " << pa_strerror(error) << std::endl;
            return false;
        }

        std::cout << "[VirtualDevice] Initialized: " << device_name << std::endl;
        return true;
    }

    bool write(const float* data, size_t frames, int channels) override {
        if (!pulse_) {
            return false;
        }

        size_t bytes = frames * channels * sizeof(float);
        int error;

        if (pa_simple_write(pulse_, data, bytes, &error) < 0) {
            std::cerr << "[VirtualDevice] Write error: " << pa_strerror(error) << std::endl;
            return false;
        }

        return true;
    }

    void close() override {
        if (pulse_) {
            pa_simple_free(pulse_);
            pulse_ = nullptr;
        }
    }

    int getSampleRate() const override { return sample_rate_; }

  private:
    pa_simple* pulse_;
    int sample_rate_;
};

std::unique_ptr<VirtualDevice> VirtualDevice::create() {
    return std::make_unique<VirtualDeviceLinux>();
}

} // namespace moonmic
