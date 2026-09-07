
#include "config.h"
#include <fstream>
#include <iostream>

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

bool Config::load(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[Config] File not found: " << path << std::endl;
            return false;
        }

        nlohmann::json j;
        file >> j;

        if (j.contains("server")) {
            auto& s = j["server"];
            if (s.contains("port")) server.port = s["port"];
            if (s.contains("bind_address")) server.bind_address = s["bind_address"];
        }

        if (j.contains("audio")) {
            auto& a = j["audio"];
            if (a.contains("stream_sample_rate")) audio.stream_sample_rate = a["stream_sample_rate"];
            if (a.contains("resampling_rate")) audio.resampling_rate = a["resampling_rate"];
            if (a.contains("sample_rate")) audio.sample_rate = a["sample_rate"]; // Backward compat
            if (a.contains("channels")) audio.channels = a["channels"];
            if (a.contains("buffer_size_ms")) audio.buffer_size_ms = a["buffer_size_ms"];
            if (a.contains("use_speaker_mode")) audio.use_speaker_mode = a["use_speaker_mode"];
            if (a.contains("driver_device_name")) audio.driver_device_name = a["driver_device_name"];
            if (a.contains("recording_endpoint_name")) audio.recording_endpoint_name = a["recording_endpoint_name"];

            if (a.contains("driver_type")) audio.driver_type = a["driver_type"];
            if (a.contains("auto_set_default_mic")) audio.auto_set_default_mic = a["auto_set_default_mic"];
            if (a.contains("original_mic_id")) audio.original_mic_id = a["original_mic_id"];
        }

        if (j.contains("security")) {
            auto& sec = j["security"];
            if (sec.contains("enable_whitelist")) security.enable_whitelist = sec["enable_whitelist"];
            if (sec.contains("sync_with_sunshine")) security.sync_with_sunshine = sec["sync_with_sunshine"];
            if (sec.contains("sunshine_state_file")) security.sunshine_state_file = sec["sunshine_state_file"];
            if (sec.contains("allowed_clients") && sec["allowed_clients"].is_array()) {
                security.allowed_clients.clear();
                for (const auto& client : sec["allowed_clients"]) {
                    security.allowed_clients.push_back(client);
                }
            }
        }

        if (j.contains("sunshine")) {
            auto sun = j["sunshine"];
            if (sun.contains("host")) sunshine.host = sun["host"];
            if (sun.contains("port")) sunshine.port = sun["port"];
            if (sun.contains("webui_port")) sunshine.webui_port = sun["webui_port"];
            if (sun.contains("paired")) sunshine.paired = sun["paired"];
            if (sun.contains("webui_logged_in")) sunshine.webui_logged_in = sun["webui_logged_in"];
            if (sun.contains("webui_username")) sunshine.webui_username = sun["webui_username"];
            if (sun.contains("webui_password_encrypted"))
                sunshine.webui_password_encrypted = sun["webui_password_encrypted"];
        }

        if (j.contains("gui")) {
            auto& g = j["gui"];
            if (g.contains("show_on_startup")) gui.show_on_startup = g["show_on_startup"];
            if (g.contains("minimize_to_tray")) gui.minimize_to_tray = g["minimize_to_tray"];
            if (g.contains("theme")) gui.theme = g["theme"];
        }

        std::cout << "[Config] Loaded from: " << path << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Config] Error loading: " << e.what() << std::endl;
        return false;
    }
}

bool Config::save(const std::string& path) {
    try {
        nlohmann::json j;

        j["server"]["port"] = server.port;
        j["server"]["bind_address"] = server.bind_address;

        j["audio"]["stream_sample_rate"] = audio.stream_sample_rate;
        j["audio"]["resampling_rate"] = audio.resampling_rate;
        j["audio"]["sample_rate"] = audio.sample_rate; // Deprecated, for backward compat
        j["audio"]["channels"] = audio.channels;
        j["audio"]["buffer_size_ms"] = audio.buffer_size_ms;
        j["audio"]["use_speaker_mode"] = audio.use_speaker_mode;
        j["audio"]["driver_device_name"] = audio.driver_device_name;
        j["audio"]["recording_endpoint_name"] = audio.recording_endpoint_name;
        j["audio"]["driver_type"] = audio.driver_type;
        j["audio"]["auto_set_default_mic"] = audio.auto_set_default_mic;
        j["audio"]["original_mic_id"] = audio.original_mic_id;

        j["security"]["enable_whitelist"] = security.enable_whitelist;
        j["security"]["sync_with_sunshine"] = security.sync_with_sunshine;
        j["security"]["sunshine_state_file"] = security.sunshine_state_file;
        j["security"]["allowed_clients"] = security.allowed_clients;

        j["sunshine"]["host"] = sunshine.host;
        j["sunshine"]["port"] = sunshine.port;
        j["sunshine"]["webui_port"] = sunshine.webui_port;
        j["sunshine"]["paired"] = sunshine.paired;
        j["sunshine"]["webui_logged_in"] = sunshine.webui_logged_in;
        j["sunshine"]["webui_username"] = sunshine.webui_username;
        j["sunshine"]["webui_password_encrypted"] = sunshine.webui_password_encrypted;

        j["gui"]["show_on_startup"] = gui.show_on_startup;
        j["gui"]["minimize_to_tray"] = gui.minimize_to_tray;
        j["gui"]["theme"] = gui.theme;

        std::ofstream file(path);
        if (!file.is_open()) {
            std::cerr << "[Config] Cannot write to: " << path << std::endl;
            return false;
        }

        file << j.dump(2);
        std::cout << "[Config] Saved to: " << path << std::endl;
        return true;
    } catch (const std::exception& e) {
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
