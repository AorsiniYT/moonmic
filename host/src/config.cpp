
#include "config.h"
#include <fstream>
#include <iostream>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

namespace moonmic {

static std::string validateConfig(const Config& config) {
    if (config.server.port < 1 || config.server.port > 65535) return "server.port must be between 1 and 65535";
    if (config.server.bind_address.empty()) return "server.bind_address cannot be empty";
    if (config.audio.resampling_rate != 0 &&
        (config.audio.resampling_rate < 8000 || config.audio.resampling_rate > 192000)) {
        return "audio.resampling_rate must be 0 or between 8000 and 192000";
    }
    if (config.audio.channels < 1 || config.audio.channels > 2) return "audio.channels must be 1 or 2";
    if (config.sunshine.host.empty()) return "sunshine.host cannot be empty";
    if (config.sunshine.webui_port < 1 || config.sunshine.webui_port > 65535) {
        return "sunshine.webui_port must be between 1 and 65535";
    }
    return {};
}

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Config] File not found: " << path << std::endl;
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;
        if (!j.is_object()) {
            std::cerr << "[Config] Root value must be an object" << std::endl;
            return false;
        }
        for (const char* section : {"server", "audio", "security", "sunshine"}) {
            if (j.contains(section) && !j.at(section).is_object()) {
                std::cerr << "[Config] " << section << " must be an object" << std::endl;
                return false;
            }
        }

        Config candidate = *this;

        if (j.contains("server")) {
            const auto& s = j.at("server");
            if (s.contains("port")) candidate.server.port = s.at("port").get<int>();
            if (s.contains("bind_address")) candidate.server.bind_address = s.at("bind_address").get<std::string>();
        }

        if (j.contains("audio")) {
            const auto& a = j.at("audio");
            if (a.contains("resampling_rate")) candidate.audio.resampling_rate = a.at("resampling_rate").get<int>();
            if (a.contains("channels")) candidate.audio.channels = a.at("channels").get<int>();
            if (a.contains("use_speaker_mode")) candidate.audio.use_speaker_mode = a.at("use_speaker_mode").get<bool>();
            if (a.contains("driver_device_name")) {
                candidate.audio.driver_device_name = a.at("driver_device_name").get<std::string>();
            }
            if (a.contains("recording_endpoint_name")) {
                candidate.audio.recording_endpoint_name = a.at("recording_endpoint_name").get<std::string>();
            }
            if (a.contains("auto_set_default_mic")) {
                candidate.audio.auto_set_default_mic = a.at("auto_set_default_mic").get<bool>();
            }
            if (a.contains("original_mic_id")) {
                candidate.audio.original_mic_id = a.at("original_mic_id").get<std::string>();
            }
        }

        if (j.contains("security")) {
            const auto& sec = j.at("security");
            if (sec.contains("enable_whitelist")) {
                candidate.security.enable_whitelist = sec.at("enable_whitelist").get<bool>();
            }
            if (sec.contains("ca_path")) candidate.security.ca_path = sec.at("ca_path").get<std::string>();
            if (sec.contains("allowed_clients")) {
                candidate.security.allowed_clients = sec.at("allowed_clients").get<std::vector<std::string>>();
            }
        }

        if (j.contains("sunshine")) {
            const auto& sun = j.at("sunshine");
            if (sun.contains("host")) candidate.sunshine.host = sun.at("host").get<std::string>();
            if (sun.contains("webui_port")) candidate.sunshine.webui_port = sun.at("webui_port").get<int>();
            if (sun.contains("paired")) candidate.sunshine.paired = sun.at("paired").get<bool>();
            if (sun.contains("webui_logged_in")) {
                candidate.sunshine.webui_logged_in = sun.at("webui_logged_in").get<bool>();
            }
            if (sun.contains("webui_username")) {
                candidate.sunshine.webui_username = sun.at("webui_username").get<std::string>();
            }
            if (sun.contains("webui_password_encrypted")) {
                candidate.sunshine.webui_password_encrypted = sun.at("webui_password_encrypted").get<std::string>();
            }
        }

        const std::string error = validateConfig(candidate);
        if (!error.empty()) {
            std::cerr << "[Config] Invalid configuration: " << error << std::endl;
            return false;
        }

        *this = std::move(candidate);
        std::cout << "[Config] Loaded from: " << path << std::endl;
        return true;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[Config] Error loading: " << e.what() << std::endl;
        return false;
    }
}

bool Config::save(const std::string& path) {
    const std::string error = validateConfig(*this);
    if (!error.empty()) {
        std::cerr << "[Config] Cannot save invalid configuration: " << error << std::endl;
        return false;
    }

    try {
        nlohmann::json j;

        j["server"]["port"] = server.port;
        j["server"]["bind_address"] = server.bind_address;

        j["audio"]["resampling_rate"] = audio.resampling_rate;
        j["audio"]["channels"] = audio.channels;
        j["audio"]["use_speaker_mode"] = audio.use_speaker_mode;
        j["audio"]["driver_device_name"] = audio.driver_device_name;
        j["audio"]["recording_endpoint_name"] = audio.recording_endpoint_name;
        j["audio"]["auto_set_default_mic"] = audio.auto_set_default_mic;
        j["audio"]["original_mic_id"] = audio.original_mic_id;

        j["security"]["enable_whitelist"] = security.enable_whitelist;
        j["security"]["ca_path"] = security.ca_path;
        j["security"]["allowed_clients"] = security.allowed_clients;

        j["sunshine"]["host"] = sunshine.host;
        j["sunshine"]["webui_port"] = sunshine.webui_port;
        j["sunshine"]["paired"] = sunshine.paired;
        j["sunshine"]["webui_logged_in"] = sunshine.webui_logged_in;
        j["sunshine"]["webui_username"] = sunshine.webui_username;
        j["sunshine"]["webui_password_encrypted"] = sunshine.webui_password_encrypted;

        std::ofstream file(path);
        if (!file.is_open()) {
            std::cerr << "[Config] Cannot write to: " << path << std::endl;
            return false;
        }

        file << j.dump(2);
        if (!file.good()) {
            std::cerr << "[Config] Failed while writing: " << path << std::endl;
            return false;
        }
        std::cout << "[Config] Saved to: " << path << std::endl;
        return true;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[Config] Error saving: " << e.what() << std::endl;
        return false;
    }
}

std::string Config::getDefaultConfigPath() {
#ifdef _WIN32
    char appdata[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
        std::string config_dir = std::string(appdata) + "\\AorsiniYT\\Moonmic";
        CreateDirectoryA((std::string(appdata) + "\\AorsiniYT").c_str(), NULL);
        CreateDirectoryA(config_dir.c_str(), NULL);
        return config_dir + "\\moonmic-host.json";
    }
    return "moonmic-host.json";
#else
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }

    if (home) {
        std::string aorsini_dir = std::string(home) + "/.config/AorsiniYT";
        std::string config_dir = aorsini_dir + "/Moonmic";
        mkdir(aorsini_dir.c_str(), 0755);
        mkdir(config_dir.c_str(), 0755);
        return config_dir + "/moonmic-host.json";
    }
    return "moonmic-host.json";
#endif
}

} // namespace moonmic
