#include "sunshine_request.h"

namespace moonmic {

std::string sunshineBase64Encode(const std::string& input) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    int value = 0;
    int bits = -6;

    for (unsigned char byte : input) {
        value = (value << 8) + byte;
        bits += 8;
        while (bits >= 0) {
            output.push_back(alphabet[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) output.push_back(alphabet[((value << 8) >> (bits + 8)) & 0x3F]);
    while (output.size() % 4)
        output.push_back('=');
    return output;
}

SunshineTlsOptions sunshineTlsOptions(const std::string& ca_path) {
    return {1L, 2L, ca_path};
}

std::string sunshineBasicAuth(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) return {};
    return "Basic " + sunshineBase64Encode(username + ":" + password);
}

} // namespace moonmic
