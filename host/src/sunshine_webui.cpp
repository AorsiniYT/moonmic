#include "logger.h"
#include "sunshine_webui.h"
#include "config.h"
#include "sunshine_request.h"
#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <exception>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

extern bool g_debug_mode;

namespace moonmic {

#ifdef _WIN32

static const char kStoredPasswordPrefix[] = "v2:";

static std::string protectPassword(const std::string& plain) {
    DATA_BLOB in{};
    DATA_BLOB out{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    in.cbData = static_cast<DWORD>(plain.size());
    if (!CryptProtectData(&in, L"moonmic", NULL, NULL, NULL, 0, &out)) {
        moonmic::logError() << "[SunshineWebUI] CryptProtectData failed" << std::endl;
        return "";
    }
    std::string blob(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return kStoredPasswordPrefix + sunshineBase64Encode(blob);
}

static std::string unprotectPassword(const std::string& stored) {
    if (stored.compare(0, sizeof(kStoredPasswordPrefix) - 1, kStoredPasswordPrefix) != 0) {
        return "";
    }
    auto blob = sunshineBase64Decode(stored.substr(sizeof(kStoredPasswordPrefix) - 1));
    if (!blob) {
        return "";
    }

    DATA_BLOB in{};
    DATA_BLOB out{};
    in.pbData = reinterpret_cast<BYTE*>(blob->data());
    in.cbData = static_cast<DWORD>(blob->size());
    if (!CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
        return "";
    }
    std::string plain(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return plain;
}

#endif

SunshineWebUI::SunshineWebUI(Config& config) : config_(config) {
#ifndef _WIN32
    config_.sunshine.webui_logged_in = false;
    config_.sunshine.webui_password_encrypted.clear();
#endif
    if (isLoggedIn()) {
        if (refreshClientList()) {
            config_.sunshine.paired = true;
            moonmic::logInfo() << "[SunshineWebUI] Session validated - auto-login successful" << std::endl;
        } else {
            config_.sunshine.paired = false;
            moonmic::logInfo() << "[SunshineWebUI] Session expired, login again" << std::endl;
        }
    }
}

std::string SunshineWebUI::generateAuthHeader() const {
    if (config_.sunshine.webui_username.empty()) {
        return "";
    }

    std::string password = session_password_;
#ifdef _WIN32
    if (password.empty()) {
        password = unprotectPassword(config_.sunshine.webui_password_encrypted);
    }
#endif
    if (password.empty()) {
        return "";
    }

    return sunshineBasicAuth(config_.sunshine.webui_username, password);
}

SunshineRequestResult SunshineWebUI::makeAuthenticatedRequestResult(const std::string& endpoint,
                                                                     const std::string& method,
                                                                     const std::string& body) {
    SunshineRequestResult result;

    if (!isLoggedIn()) {
        moonmic::logError() << "[SunshineWebUI] Not logged in" << std::endl;
        return result;
    }

    std::string url = "https://" + config_.sunshine.host + ":" + std::to_string(config_.sunshine.webui_port) + endpoint;

    std::string auth_header = generateAuthHeader();
    if (auth_header.empty()) {
        moonmic::logError() << "[SunshineWebUI] Failed to generate auth header" << std::endl;
        return result;
    }

    if (g_debug_mode) {
        moonmic::logInfo() << "[SunshineWebUI] Request: " << method << " " << url << std::endl;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        moonmic::logError() << "[SunshineWebUI] Failed to initialize curl" << std::endl;
        return result;
    }

    std::string response_string;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        }
    }

    struct curl_slist* headers = NULL;
    std::string auth_header_full = "Authorization: " + auth_header;
    headers = curl_slist_append(headers, auth_header_full.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const auto tls = sunshineTlsOptions(config_.security.ca_path);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, tls.verify_peer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, tls.verify_host);
    if (!tls.ca_path.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, tls.ca_path.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            std::string* str = static_cast<std::string*>(userdata);
            str->append(ptr, size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

    const CURLcode curl_result = curl_easy_perform(curl);
    long http_code = 0;
    if (curl_result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    result.status = classifySunshineResponse(curl_result == CURLE_OK, curl_result == CURLE_GOT_NOTHING, http_code);
    result.http_code = http_code;
    result.body = std::move(response_string);

    if (g_debug_mode && curl_result == CURLE_OK) {
        moonmic::logInfo() << "[SunshineWebUI] HTTP " << http_code << " - " << result.body.size() << " bytes received"
                  << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result.status == SunshineRequestStatus::TransportError) {
        moonmic::logError() << "[SunshineWebUI] Request failed: " << curl_easy_strerror(curl_result) << std::endl;
    } else if (result.status == SunshineRequestStatus::HttpError) {
        if (http_code == 401) {
            moonmic::logError() << "[SunshineWebUI] Authentication failed (401 Unauthorized)" << std::endl;
        } else {
            moonmic::logError() << "[SunshineWebUI] HTTP error: " << http_code << std::endl;
        }
    }

    return result;
}

std::string SunshineWebUI::makeAuthenticatedRequest(const std::string& endpoint, const std::string& method,
                                                    const std::string& body) {
    auto result = makeAuthenticatedRequestResult(endpoint, method, body);
    if (result.status != SunshineRequestStatus::Success) {
        return "";
    }
    return std::move(result.body);
}

bool SunshineWebUI::restartSunshine() {
    const auto result = makeAuthenticatedRequestResult("/api/restart", "POST", "{}");
    if (!sunshineRestartSucceeded(result.status)) {
        moonmic::logError() << "[SunshineWebUI] Sunshine restart request failed" << std::endl;
        return false;
    }

    if (result.status == SunshineRequestStatus::EmptyReply) {
        moonmic::logInfo() << "[SunshineWebUI] Sunshine closed the connection during restart" << std::endl;
    } else {
        moonmic::logInfo() << "[SunshineWebUI] Sunshine restart requested" << std::endl;
    }
    return true;
}

bool SunshineWebUI::login(const std::string& username, const std::string& password) {
    moonmic::logInfo() << "[SunshineWebUI] Attempting login for user: " << username << std::endl;

    std::string old_username = config_.sunshine.webui_username;
    std::string old_password = config_.sunshine.webui_password_encrypted;
    std::string old_session_password = session_password_;
    bool old_logged_in = config_.sunshine.webui_logged_in;

    config_.sunshine.webui_username = username;
    session_password_ = password;
#ifdef _WIN32
    config_.sunshine.webui_password_encrypted = protectPassword(password);
    if (config_.sunshine.webui_password_encrypted.empty()) {
        config_.sunshine.webui_username = old_username;
        session_password_ = old_session_password;
        return false;
    }
#else
    config_.sunshine.webui_password_encrypted.clear();
#endif
    config_.sunshine.webui_logged_in = true;

    std::string response = makeAuthenticatedRequest("/api/clients/list");

    if (response.empty()) {

        config_.sunshine.webui_username = old_username;
        config_.sunshine.webui_password_encrypted = old_password;
        session_password_ = old_session_password;
        config_.sunshine.webui_logged_in = old_logged_in;

        moonmic::logError() << "[SunshineWebUI] Login failed - invalid credentials" << std::endl;
        return false;
    }

    config_.sunshine.paired = true;
    saveCredentials();

    try {
        nlohmann::json json_response = nlohmann::json::parse(response);

        if (json_response.contains("named_certs")) {
            paired_clients_.clear();

            for (const auto& cert : json_response["named_certs"]) {
                WebUIPairedClient client;
                client.name = cert.value("name", "Unknown");
                client.uuid = cert.value("uuid", "");
                paired_clients_.push_back(client);
            }

            moonmic::logInfo() << "[SunshineWebUI] Login successful - loaded " << paired_clients_.size() << " paired client(s)"
                      << std::endl;
        }
    } catch (const std::exception& e) {
        moonmic::logError() << "[SunshineWebUI] Error parsing client list: " << e.what() << std::endl;
    }

    return true;
}

bool SunshineWebUI::isLoggedIn() const {
    return config_.sunshine.webui_logged_in && !config_.sunshine.webui_username.empty();
}

void SunshineWebUI::logout() {
    config_.sunshine.webui_username = "";
    config_.sunshine.webui_password_encrypted = "";
    session_password_.clear();
    config_.sunshine.webui_logged_in = false;
    config_.sunshine.paired = false;
    paired_clients_.clear();

    saveCredentials();

    moonmic::logInfo() << "[SunshineWebUI] Logged out" << std::endl;
}

std::vector<WebUIPairedClient> SunshineWebUI::getPairedClients() {
    if (!isLoggedIn()) {
        return {};
    }

    return paired_clients_;
}

// because Sunshine generates random UUID during pairing (nvhttp.cpp line 274),

bool SunshineWebUI::refreshClientList() {
    if (!isLoggedIn()) {
        return false;
    }

    moonmic::logInfo() << "[SunshineWebUI] Refreshing client list..." << std::endl;

    std::string response = makeAuthenticatedRequest("/api/clients/list");

    if (response.empty()) {
        moonmic::logError() << "[SunshineWebUI] Failed to refresh client list" << std::endl;
        return false;
    }

    try {
        nlohmann::json json_response = nlohmann::json::parse(response);

        if (json_response.contains("named_certs")) {
            paired_clients_.clear();

            for (const auto& cert : json_response["named_certs"]) {
                WebUIPairedClient client;
                client.name = cert.value("name", "Unknown");
                client.uuid = cert.value("uuid", "");
                paired_clients_.push_back(client);
            }

            moonmic::logInfo() << "[SunshineWebUI] Loaded " << paired_clients_.size() << " paired client(s)" << std::endl;
        }
        return true;
    } catch (const std::exception& e) {
        moonmic::logError() << "[SunshineWebUI] Error parsing client list: " << e.what() << std::endl;
        return false;
    }
}

void SunshineWebUI::saveCredentials() {

    config_.save(Config::getDefaultConfigPath());

    moonmic::logInfo() << "[SunshineWebUI] Credentials saved" << std::endl;
}

bool SunshineWebUI::setDisplayResolution(uint16_t target_width, uint16_t target_height) {
    if (!isLoggedIn()) {
        moonmic::logError() << "[SunshineWebUI] Not logged in - cannot set resolution" << std::endl;
        return false;
    }

    std::string target_res = std::to_string(target_width) + "x" + std::to_string(target_height);
    moonmic::logInfo() << "[SunshineWebUI] Verifying display resolution: 960x544 -> " << target_res << std::endl;

    std::string current_config_str = makeAuthenticatedRequest("/api/config");
    if (current_config_str.empty()) {
        moonmic::logError() << "[SunshineWebUI] Failed to get current config for verification. Aborting update to prevent "
                     "restart loops."
                  << std::endl;
        return false;
    } else {
        try {
            nlohmann::json current = nlohmann::json::parse(current_config_str);

            if (current.contains("dd_mode_remapping")) {
                std::string remapping_str = current["dd_mode_remapping"];

                nlohmann::json remapping = nlohmann::json::parse(remapping_str);

                if (remapping.contains("mixed") && remapping["mixed"].is_array()) {
                    for (const auto& entry : remapping["mixed"]) {

                        if (entry.value("requested_resolution", "") == "960x544") {

                            if (entry.value("final_resolution", "") == target_res) {
                                moonmic::logInfo() << "[SunshineWebUI] Resolution already set to " << target_res
                                          << " - Skipping update/restart" << std::endl;
                                return true;
                            } else {
                                moonmic::logInfo() << "[SunshineWebUI] Found existing mapping but different resolution: "
                                          << entry.value("final_resolution", "") << " (wanted " << target_res << ")"
                                          << std::endl;
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            moonmic::logError() << "[SunshineWebUI] Error parsing current config: " << e.what() << std::endl;
        }
    }

    nlohmann::json remapping_json;
    remapping_json["mixed"] = nlohmann::json::array();
    remapping_json["resolution_only"] = nlohmann::json::array();
    remapping_json["refresh_rate_only"] = nlohmann::json::array();

    nlohmann::json entry;
    entry["requested_resolution"] = "960x544";
    entry["requested_fps"] = "";
    entry["final_resolution"] = target_res;
    entry["final_refresh_rate"] = "60";
    remapping_json["mixed"].push_back(entry);

    std::string remapping_str = remapping_json.dump();

    nlohmann::json config;
    config["dd_mode_remapping"] = remapping_str;
    config["dd_configuration_option"] = "ensure_active";

    moonmic::logInfo() << "[SunshineWebUI] Applying new resolution config..." << std::endl;

    std::string config_json = config.dump();
    std::string response = makeAuthenticatedRequest("/api/config", "POST", config_json);

    if (response.empty()) {
        moonmic::logError() << "[SunshineWebUI] Failed to save config" << std::endl;
        return false;
    }

    moonmic::logInfo() << "[SunshineWebUI] Config save response: " << response << std::endl;
    moonmic::logInfo() << "[SunshineWebUI] Display resolution configured: 960x544 -> " << target_res << std::endl;
    return true;
}

bool SunshineWebUI::getCurrentResolution(uint16_t& width, uint16_t& height) {
    if (!isLoggedIn()) return false;

    std::string current_config_str = makeAuthenticatedRequest("/api/config");
    if (current_config_str.empty()) return false;

    try {
        nlohmann::json current = nlohmann::json::parse(current_config_str);
        if (current.contains("dd_mode_remapping")) {
            std::string remapping_str = current["dd_mode_remapping"];
            nlohmann::json remapping = nlohmann::json::parse(remapping_str);

            if (remapping.contains("mixed") && remapping["mixed"].is_array()) {
                for (const auto& entry : remapping["mixed"]) {
                    if (entry.value("requested_resolution", "") == "960x544") {
                        std::string res = entry.value("final_resolution", "");
                        if (sscanf(res.c_str(), "%hux%hu", &width, &height) == 2) {
                            return true;
                        }
                    }
                }
            }
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool SunshineWebUI::clearDisplayResolution() {
    if (!isLoggedIn()) {
        moonmic::logError() << "[SunshineWebUI] Not logged in - cannot clear resolution" << std::endl;
        return false;
    }

    moonmic::logInfo() << "[SunshineWebUI] Clearing display resolution remapping" << std::endl;

    std::string current_config = makeAuthenticatedRequest("/api/config");
    if (current_config.empty()) {
        moonmic::logError() << "[SunshineWebUI] Failed to get current config" << std::endl;
        return false;
    }

    try {

        nlohmann::json config_json = nlohmann::json::parse(current_config);

        if (config_json.contains("dd") && config_json["dd"].contains("mode_remapping") &&
            config_json["dd"]["mode_remapping"].contains("mixed")) {

            auto& mixed_array = config_json["dd"]["mode_remapping"]["mixed"];

            mixed_array.erase(std::remove_if(mixed_array.begin(), mixed_array.end(),
                                             [](const nlohmann::json& entry) {
                                                 return entry.value("requested_resolution", "") == "960x544";
                                             }),
                              mixed_array.end());

            std::string updated_config = config_json.dump();
            std::string response = makeAuthenticatedRequest("/api/config", "POST", updated_config);

            if (response.empty()) {
                moonmic::logError() << "[SunshineWebUI] Failed to save cleared config" << std::endl;
                return false;
            }

            moonmic::logInfo() << "[SunshineWebUI] Display resolution mapping cleared" << std::endl;
            return true;
        }

        moonmic::logInfo() << "[SunshineWebUI] No resolution remapping to clear" << std::endl;
        return true;

    } catch (const std::exception& e) {
        moonmic::logError() << "[SunshineWebUI] Error clearing resolution: " << e.what() << std::endl;
        return false;
    }
}

} // namespace moonmic
