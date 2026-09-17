#pragma once

#include <optional>
#include <string>

namespace moonmic {

struct SunshineTlsOptions {
    long verify_peer;
    long verify_host;
    std::string ca_path;
};

enum class SunshineRequestStatus {
    Success,
    EmptyReply,
    TransportError,
    HttpError,
};

struct SunshineRequestResult {
    SunshineRequestStatus status = SunshineRequestStatus::TransportError;
    long http_code = 0;
    std::string body;
};

SunshineTlsOptions sunshineTlsOptions(const std::string& ca_path);
SunshineRequestStatus classifySunshineResponse(bool transport_ok, bool empty_reply, long http_code);
bool sunshineRestartSucceeded(SunshineRequestStatus status);
std::string sunshineBase64Encode(const std::string& input);
std::optional<std::string> sunshineBase64Decode(const std::string& input);
std::string sunshineBasicAuth(const std::string& username, const std::string& password);

} // namespace moonmic
