
#include "audio_utils.h"
#include "audio_device_manager.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys.h>

#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>

#include <setupapi.h>
#include <cfgmgr32.h>
#include <iostream>

#ifndef PKEY_Device_FriendlyName
DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0,
                   14);
#endif

namespace moonmic {
namespace platform {
namespace windows {

bool GetDefaultRecordingDevice(std::string& deviceId, std::string& friendlyName) {
    AudioDeviceManager manager;
    AudioDeviceInfo info = manager.getCurrentDefaultRecordingDevice();

    if (info.id.empty()) {
        return false;
    }

    deviceId = info.id;
    friendlyName = info.name;
    return true;
}

std::string FindRecordingDeviceID(const std::string& name) {
    if (name.empty()) return "";

    AudioDeviceManager manager;
    std::vector<AudioDeviceInfo> devices = manager.enumerateRecordingDevices();

    for (const auto& device : devices) {

        if (device.id == name || device.name.find(name) != std::string::npos) {
            return device.id;
        }
    }

    return "";
}

bool SetDefaultRecordingDevice(const std::string& nameOrId) {
    if (nameOrId.empty()) return false;

    std::string targetId = nameOrId;

    if (targetId.find("{") == std::string::npos) {
        std::string resolvedId = FindRecordingDeviceID(targetId);
        if (!resolvedId.empty()) {
            targetId = resolvedId;
            std::cout << "[AudioUtils] Resolved device name '" << nameOrId << "' to ID: " << targetId << std::endl;
        } else {
            std::cerr << "[AudioUtils] Could not find recording device matching: " << nameOrId << std::endl;
            return false;
        }
    }

    AudioDeviceManager manager;
    return manager.setDefaultRecordingDevice(targetId);
}

bool IsRunningAsAdmin() {
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdminSID = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                 0, &pAdminSID)) {
        if (!CheckTokenMembership(NULL, pAdminSID, &fIsRunAsAdmin)) {
            fIsRunAsAdmin = FALSE;
        }
        FreeSid(pAdminSID);
    }
    return fIsRunAsAdmin;
}

bool ChangeDeviceState(const std::string& name, bool enable) {
    if (name.empty()) return false;

    HDEVINFO hDevInfo;
    SP_DEVINFO_DATA DeviceInfoData;
    DWORD i;

    hDevInfo = SetupDiGetClassDevs(NULL, NULL, 0, DIGCF_PRESENT | DIGCF_ALLCLASSES);

    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    bool success = false;

    for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData); i++) {
        DWORD DataT;
        char friendlyName[256];

        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &DeviceInfoData, SPDRP_FRIENDLYNAME, &DataT,
                                              (PBYTE)friendlyName, sizeof(friendlyName), NULL)) {

            if (std::string(friendlyName).find(name) != std::string::npos) {
                std::cout << "[AudioUtils] Found device for state change: " << friendlyName << std::endl;

                SP_PROPCHANGE_PARAMS params;
                params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
                params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
                params.StateChange = enable ? DICS_ENABLE : DICS_DISABLE;
                params.Scope = DICS_FLAG_GLOBAL;
                params.HwProfile = 0;

                if (SetupDiSetClassInstallParams(hDevInfo, &DeviceInfoData, (SP_CLASSINSTALL_HEADER*)&params,
                                                 sizeof(params))) {
                    if (SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, &DeviceInfoData)) {
                        std::cout << "[AudioUtils] Successfully changed state to: " << (enable ? "Enabled" : "Disabled")
                                  << " for: " << friendlyName << std::endl;
                        success = true;
                    } else {
                        std::cerr << "[AudioUtils] SetupDiCallClassInstaller failed. Error: " << GetLastError()
                                  << std::endl;
                    }
                } else {
                    std::cerr << "[AudioUtils] SetupDiSetClassInstallParams failed." << std::endl;
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return success;
}

} // namespace windows
} // namespace platform
} // namespace moonmic
