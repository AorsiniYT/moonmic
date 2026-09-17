#include "sunshine_request.h"

#include <cstdint>

namespace moonmic {

namespace {

int base64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

} // namespace

std::string sunshineBase64Encode(const std::string& input) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    for (size_t offset = 0; offset < input.size(); offset += 3) {
        const uint32_t first = static_cast<unsigned char>(input[offset]);
        const bool has_second = offset + 1 < input.size();
        const bool has_third = offset + 2 < input.size();
        const uint32_t second = has_second ? static_cast<unsigned char>(input[offset + 1]) : 0;
        const uint32_t third = has_third ? static_cast<unsigned char>(input[offset + 2]) : 0;
        const uint32_t packed = (first << 16) | (second << 8) | third;

        output.push_back(alphabet[(packed >> 18) & 0x3F]);
        output.push_back(alphabet[(packed >> 12) & 0x3F]);
        output.push_back(has_second ? alphabet[(packed >> 6) & 0x3F] : '=');
        output.push_back(has_third ? alphabet[packed & 0x3F] : '=');
    }

    return output;
}

std::optional<std::string> sunshineBase64Decode(const std::string& input) {
    if (input.empty()) return std::string{};
    if (input.size() % 4 != 0) return std::nullopt;

    std::string output;
    output.reserve((input.size() / 4) * 3);

    for (size_t offset = 0; offset < input.size(); offset += 4) {
        const int first = base64Value(static_cast<unsigned char>(input[offset]));
        const int second = base64Value(static_cast<unsigned char>(input[offset + 1]));
        const bool third_padding = input[offset + 2] == '=';
        const bool fourth_padding = input[offset + 3] == '=';

        if (first < 0 || second < 0 || (third_padding && !fourth_padding)) return std::nullopt;
        if ((third_padding || fourth_padding) && offset + 4 != input.size()) return std::nullopt;

        const int third = third_padding ? 0 : base64Value(static_cast<unsigned char>(input[offset + 2]));
        const int fourth = fourth_padding ? 0 : base64Value(static_cast<unsigned char>(input[offset + 3]));
        if (third < 0 || fourth < 0) return std::nullopt;

        if (third_padding && (second & 0x0F) != 0) return std::nullopt;
        if (!third_padding && fourth_padding && (third & 0x03) != 0) return std::nullopt;

        const uint32_t packed = (static_cast<uint32_t>(first) << 18) | (static_cast<uint32_t>(second) << 12) |
                                (static_cast<uint32_t>(third) << 6) | static_cast<uint32_t>(fourth);

        output.push_back(static_cast<char>((packed >> 16) & 0xFF));
        if (!third_padding) output.push_back(static_cast<char>((packed >> 8) & 0xFF));
        if (!fourth_padding) output.push_back(static_cast<char>(packed & 0xFF));
    }

    return output;
}

SunshineTlsOptions sunshineTlsOptions(const std::string& ca_path) {
    return {1L, 2L, ca_path};
}

SunshineRequestStatus classifySunshineResponse(bool transport_ok, bool empty_reply, long http_code) {
    if (empty_reply) return SunshineRequestStatus::EmptyReply;
    if (!transport_ok) return SunshineRequestStatus::TransportError;
    if (http_code != 200) return SunshineRequestStatus::HttpError;
    return SunshineRequestStatus::Success;
}

bool sunshineRestartSucceeded(SunshineRequestStatus status) {
    return status == SunshineRequestStatus::Success || status == SunshineRequestStatus::EmptyReply;
}

std::string sunshineBasicAuth(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) return {};
    return "Basic " + sunshineBase64Encode(username + ":" + password);
}

} // namespace moonmic
