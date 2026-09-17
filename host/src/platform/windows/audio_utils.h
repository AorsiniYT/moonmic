
#pragma once

#include <string>
#include <vector>

namespace moonmic {
namespace platform {
namespace windows {

std::string WideToUtf8(const wchar_t* value);

bool GetDefaultRecordingDevice(std::string& deviceId, std::string& friendlyName);

bool SetDefaultRecordingDevice(const std::string& nameOrId);

std::string FindRecordingDeviceID(const std::string& name);

/**
 * @brief Enable or Disable a device driver by name (Requires Admin)
 * @param name Part of the device friendly name (e.g. "VB-Cable")
 * @param enable true to enable, false to disable
 * @return true if successful
 */
bool ChangeDeviceState(const std::string& name, bool enable);

bool IsRunningAsAdmin();

} // namespace windows
} // namespace platform
} // namespace moonmic
