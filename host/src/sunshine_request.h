#pragma once

#include <string>

namespace moonmic {

struct SunshineTlsOptions {
    long verify_peer;
    long verify_host;
    std::string ca_path;
};

SunshineTlsOptions sunshineTlsOptions(const std::string& ca_path);
std::string sunshineBase64Encode(const std::string& input);
std::string sunshineBasicAuth(const std::string& username, const std::string& password);

} // namespace moonmic
