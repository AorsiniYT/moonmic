
#include "version_checker.h"
#include "version.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <array>
#include <optional>
#include <sstream>
#include <iostream>

namespace moonmic {

static const char* LATEST_RELEASE_URL = "https://api.github.com/repos/AorsiniYT/moonmic/releases/latest";
static const char* RELEASES_URL = "https://github.com/AorsiniYT/moonmic/releases";

std::string VersionChecker::getCurrentVersion() {
    return MOONMIC_VERSION;
}

static std::optional<std::array<int, 3>> parseVersion(const std::string& version) {
    std::istringstream stream(version);
    std::array<int, 3> parts{};
    char first_dot = 0;
    char second_dot = 0;
    char trailing = 0;
    if (!(stream >> parts[0] >> first_dot >> parts[1] >> second_dot >> parts[2]) || first_dot != '.' ||
        second_dot != '.' || (stream >> trailing) || parts[0] < 0 || parts[1] < 0 || parts[2] < 0) {
        return std::nullopt;
    }
    return parts;
}

int VersionChecker::compareVersions(const std::string& v1, const std::string& v2) {
    const auto first = parseVersion(v1);
    const auto second = parseVersion(v2);
    if (!first || !second) return 0;

    if ((*first)[0] != (*second)[0]) return ((*first)[0] > (*second)[0]) ? 1 : -1;
    if ((*first)[1] != (*second)[1]) return ((*first)[1] > (*second)[1]) ? 1 : -1;
    if ((*first)[2] != (*second)[2]) return ((*first)[2] > (*second)[2]) ? 1 : -1;
    return 0;
}

bool VersionChecker::fetchLatestRelease(std::string& version, std::string& download_url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[VersionChecker] Failed to initialize curl" << std::endl;
        return false;
    }

    std::string result;

    curl_easy_setopt(curl, CURLOPT_URL, LATEST_RELEASE_URL);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Moonmic version checker");
    curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

    const CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[VersionChecker] Version fetch failed: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    if (response_code != 200) {
        std::cerr << "[VersionChecker] Release request returned HTTP " << response_code << std::endl;
        return false;
    }

    const auto release = nlohmann::json::parse(result, nullptr, false);
    if (release.is_discarded() || !release.contains("tag_name") || !release["tag_name"].is_string()) {
        std::cerr << "[VersionChecker] Release response did not contain a valid tag" << std::endl;
        return false;
    }

    version = release["tag_name"].get<std::string>();
    if (!version.empty() && (version.front() == 'v' || version.front() == 'V')) {
        version.erase(version.begin());
    }
    if (!parseVersion(version)) {
        std::cerr << "[VersionChecker] Release tag is not a semantic version: " << version << std::endl;
        return false;
    }

    if (release.contains("html_url") && release["html_url"].is_string()) {
        download_url = release["html_url"].get<std::string>();
    } else {
        download_url = RELEASES_URL;
    }
    return true;
}

VersionChecker::VersionInfo VersionChecker::checkForUpdates() {
    VersionInfo info{};
    info.current_version = getCurrentVersion();
    info.download_url = RELEASES_URL;
    if (fetchLatestRelease(info.latest_version, info.download_url)) {
        info.update_available = compareVersions(info.latest_version, info.current_version) > 0;
    }
    return info;
}

} // namespace moonmic
