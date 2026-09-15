
#pragma once

#include <string>

namespace moonmic {

class VersionChecker {
  public:
    struct VersionInfo {
        std::string current_version;
        std::string latest_version;
        bool update_available;
        std::string download_url;
    };

    static VersionInfo checkForUpdates();

    static std::string getCurrentVersion();

    static int compareVersions(const std::string& v1, const std::string& v2);

  private:
    static bool fetchLatestRelease(std::string& version, std::string& download_url);
};

} // namespace moonmic
