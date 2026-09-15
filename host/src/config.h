
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
        int resampling_rate = 0;
        int channels = 1;
        bool use_speaker_mode = false;
        std::string driver_device_name = "VB-Audio Virtual Cable";
        std::string recording_endpoint_name = "CABLE Output";

        bool auto_set_default_mic = true;
        std::string original_mic_id;
    } audio;

    struct {
        bool enable_whitelist = true;
        std::vector<std::string> allowed_clients;
        std::string ca_path;
    } security;

    struct {
        std::string host = "localhost";
        int webui_port = 47990;
        bool paired = false;

        bool webui_logged_in = false;
        std::string webui_username;
        std::string webui_password_encrypted;
    } sunshine;

    bool load(const std::string& path);

    bool save(const std::string& path);

    static std::string getDefaultConfigPath();
};

} // namespace moonmic
