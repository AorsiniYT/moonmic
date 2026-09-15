#include "config.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

static bool sameConfig(const moonmic::Config& first, const moonmic::Config& second) {
    return first.server.port == second.server.port && first.server.bind_address == second.server.bind_address &&
           first.audio.resampling_rate == second.audio.resampling_rate &&
           first.audio.channels == second.audio.channels &&
           first.audio.use_speaker_mode == second.audio.use_speaker_mode &&
           first.audio.driver_device_name == second.audio.driver_device_name &&
           first.audio.recording_endpoint_name == second.audio.recording_endpoint_name &&
           first.audio.auto_set_default_mic == second.audio.auto_set_default_mic &&
           first.audio.original_mic_id == second.audio.original_mic_id &&
           first.security.enable_whitelist == second.security.enable_whitelist &&
           first.security.ca_path == second.security.ca_path &&
           first.security.allowed_clients == second.security.allowed_clients &&
           first.sunshine.host == second.sunshine.host &&
           first.sunshine.webui_port == second.sunshine.webui_port && first.sunshine.paired == second.sunshine.paired &&
           first.sunshine.webui_logged_in == second.sunshine.webui_logged_in &&
           first.sunshine.webui_username == second.sunshine.webui_username &&
           first.sunshine.webui_password_encrypted == second.sunshine.webui_password_encrypted;
}

int main() {
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path =
        std::filesystem::temp_directory_path() / ("moonmic-config-test-" + std::to_string(suffix) + ".json");

    moonmic::Config expected;
    expected.server.port = 49123;
    expected.server.bind_address = "127.0.0.1";
    expected.audio.resampling_rate = 48000;
    expected.audio.channels = 2;
    expected.audio.use_speaker_mode = true;
    expected.audio.driver_device_name = "Test playback";
    expected.audio.recording_endpoint_name = "Test capture";
    expected.audio.auto_set_default_mic = false;
    expected.audio.original_mic_id = "original-device";
    expected.security.enable_whitelist = false;
    expected.security.ca_path = "test-ca.pem";
    expected.security.allowed_clients = {"client-a", "client-b"};
    expected.sunshine.host = "sunshine.local";
    expected.sunshine.webui_port = 47981;
    expected.sunshine.paired = true;
    expected.sunshine.webui_logged_in = true;
    expected.sunshine.webui_username = "moonmic";
    expected.sunshine.webui_password_encrypted = "protected";

    if (!expected.save(path.string())) return 1;

    {
        std::ifstream saved(path);
        nlohmann::json persisted;
        saved >> persisted;
        if (persisted.contains("gui") || persisted["audio"].contains("driver_type") ||
            persisted["security"].contains("sync_with_sunshine") ||
            persisted["security"].contains("sunshine_state_file") || persisted["sunshine"].contains("port")) {
            return 5;
        }
    }

    moonmic::Config loaded;
    if (!loaded.load(path.string()) || !sameConfig(expected, loaded)) return 2;

    const int valid_port = loaded.server.port;
    {
        std::ofstream invalid(path);
        invalid << R"({"server":{"port":70000}})";
    }
    if (loaded.load(path.string()) || loaded.server.port != valid_port) return 3;

    std::error_code error;
    std::filesystem::remove(path, error);
    return error ? 4 : 0;
}
