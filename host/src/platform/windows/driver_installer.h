
#pragma once

#include <string>

namespace moonmic {

class DriverInstaller {
public:
    DriverInstaller();

    std::string getVBCableInputDevice();
    std::string getVBCableOutputDevice();

    static bool isRunningAsAdmin();

    static bool restartAsAdmin();

    bool isVBCableInstalled();

    bool isSteamMicrophoneInstalled();

    bool isAnyDriverInstalled();

    bool installVBCable();

    bool uninstallVBCable();

    // Install Steam Streaming Microphone driver (requires external files)
    bool installSteamMicrophone();

    bool uninstallSteamMicrophone();

private:
    std::string driver_path_;

    bool extractResourceToFile(const char* resource_name, const std::string& output_path);

    bool extractEmbeddedSteamDriver(const std::string& temp_dir, bool is_x64);

    bool runSetupExecutable(bool uninstall = false);

    bool extractEmbeddedInstaller(const std::string& temp_dir);
    void cleanupTempDir(const std::string& temp_dir);

    bool createRootDevice(const std::string& hardwareId, const std::string& infPath);

    void removeDevicesByHardwareId(const std::string& hardwareId);

    bool disableSteamStreamingSpeakers();
};

}
