
#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "sunshine_request.h"

namespace moonmic {

struct Config;

struct WebUIPairedClient {
    std::string name;
    std::string uuid;
};

class SunshineWebUI {
  public:
    SunshineWebUI(Config& config);

    bool login(const std::string& username, const std::string& password);

    bool isLoggedIn() const;

    void logout();

    /**
     * @brief Get list of all paired clients from Sunshine
     * Requires valid login
     * @return Vector of paired clients, empty if not logged in
     */
    std::vector<WebUIPairedClient> getPairedClients();

    // because Sunshine generates random UUID during pairing, ignoring client's uniqueid.

    bool refreshClientList();

    bool setDisplayResolution(uint16_t target_width, uint16_t target_height);

    bool getCurrentResolution(uint16_t& width, uint16_t& height);

    bool restartSunshine();

    bool clearDisplayResolution();

    std::string makeAuthenticatedRequest(const std::string& endpoint, const std::string& method = "GET",
                                         const std::string& body = "");

  private:
    Config& config_;
    std::vector<WebUIPairedClient> paired_clients_;
    std::string session_password_;

    std::string generateAuthHeader() const;
    SunshineRequestResult makeAuthenticatedRequestResult(const std::string& endpoint, const std::string& method,
                                                         const std::string& body);

    void saveCredentials();
};

} // namespace moonmic
