#include "logger.h"

#include "guardian_state.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace moonmic {

std::string GuardianStateManager::getStatePath() {
#ifdef _WIN32
    char appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        std::string path = std::string(appdata) + "\\AorsiniYT\\Moonmic";
        std::filesystem::create_directories(path);
        return path + "\\moonmic.state";
    }
#else
    const char* home = std::getenv("HOME");
    if (home) {
        std::string path = std::string(home) + "/.config/AorsiniYT/Moonmic";
        std::filesystem::create_directories(path);
        return path + "/moonmic.state";
    }
#endif
    return "./moonmic.state"; // Fallback
}

bool GuardianStateManager::writeState(const GuardianState& state) {
    try {
        nlohmann::json j;
        j["original_mic_id"] = state.original_mic_id;
        j["original_mic_name"] = state.original_mic_name;
        j["host_pid"] = state.host_pid;
        j["timestamp"] = state.timestamp;

        std::ofstream file(getStatePath());
        if (!file.is_open()) {
            moonmic::logError() << "[GuardianState] Failed to open state file for writing" << std::endl;
            return false;
        }

        file << j.dump(2);
        file.close();

        moonmic::logInfo() << "[GuardianState] State saved: " << state.original_mic_name << std::endl;
        return true;
    } catch (const std::exception& e) {
        moonmic::logError() << "[GuardianState] Write error: " << e.what() << std::endl;
        return false;
    }
}

bool GuardianStateManager::readState(GuardianState& state) {
    try {
        std::ifstream file(getStatePath());
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        state.original_mic_id = j.value("original_mic_id", "");
        state.original_mic_name = j.value("original_mic_name", "");
        state.host_pid = j.value("host_pid", 0UL);
        state.timestamp = j.value("timestamp", 0L);

        return !state.original_mic_id.empty();
    } catch (const std::exception& e) {
        moonmic::logError() << "[GuardianState] Read error: " << e.what() << std::endl;
        return false;
    }
}

void GuardianStateManager::deleteState() {
    try {
        std::filesystem::remove(getStatePath());
        moonmic::logInfo() << "[GuardianState] State file deleted" << std::endl;
    } catch (const std::exception& e) {
        moonmic::logError() << "[GuardianState] Delete error: " << e.what() << std::endl;
    }
}

bool GuardianStateManager::stateExists() {
    return std::filesystem::exists(getStatePath());
}

} // namespace moonmic
