
#pragma once

#include <string>

namespace moonmic {

class GuardianLauncher {
  public:
    static bool launchGuardian(const std::string& original_mic_id, const std::string& original_mic_name);

    static void signalNormalShutdown();

    static void signalRestart();

    static bool isGuardianRunning();

    static constexpr const char* SHUTDOWN_EVENT_NAME = "Local\\MoonmicHostShutdown";
    static constexpr const char* RESTART_EVENT_NAME = "Local\\MoonmicHostRestart";
};

} // namespace moonmic
