
#pragma once

#include <string>
#include <functional>

namespace moonmic {

class VersionChecker {
  public:
    struct VersionInfo {
        std::string current_version;
        std::string latest_version;
        bool update_available;
        std::string download_url;
    };

    VersionChecker();
    ~VersionChecker();

    void checkForUpdates(std::function<void(const VersionInfo&)> callback);

    static std::string getCurrentVersion();

    static int compareVersions(const std::string& v1, const std::string& v2);

  private:
    std::string fetchLatestVersion();
};

} // namespace moonmic
