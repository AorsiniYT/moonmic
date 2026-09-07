
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace moonmic {

struct Config {

    struct {
        int port = 48100;
        std::string bind_address = "0.0.0.0";
    } server;

    struct {
        int stream_sample_rate = 16000;
        int resampling_rate = 0;
        int sample_rate = 0;
        int channels = 1;
        int buffer_size_ms = 20;
        bool use_speaker_mode = false;
        std::string driver_type = "VBCABLE";
        std::string driver_device_name = "VB-Audio Virtual Cable";
        std::string recording_endpoint_name = "CABLE Output";

        bool auto_set_default_mic = true;
        bool use_virtual_mic = true;
        std::string original_mic_id;
    } audio;

    struct {
        bool enable_whitelist = true;
        bool sync_with_sunshine = true;
        std::string sunshine_state_file;
        std::vector<std::string> allowed_clients;
    } security;

    struct {
        std::string host = "localhost";
        int port = 47989;
        int webui_port = 47990;
        bool paired = false;

        bool webui_logged_in = false;
        std::string webui_username;
        std::string webui_password_encrypted;
    } sunshine;

    struct {
        bool show_on_startup = true;
        bool minimize_to_tray = true;
        std::string theme = "dark";
    } gui;

    bool load(const std::string& path);

    bool save(const std::string& path);

    static std::string getDefaultConfigPath();
};

} // namespace moonmic
