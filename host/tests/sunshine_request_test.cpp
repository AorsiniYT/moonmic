#include "sunshine_request.h"

int main() {
    const auto defaults = moonmic::sunshineTlsOptions("");
    if (defaults.verify_peer != 1L || defaults.verify_host != 2L || !defaults.ca_path.empty()) return 1;

    const auto private_ca = moonmic::sunshineTlsOptions("sunshine-ca.pem");
    if (private_ca.verify_peer != 1L || private_ca.verify_host != 2L || private_ca.ca_path != "sunshine-ca.pem") {
        return 2;
    }

    if (moonmic::sunshineBasicAuth("moonmic", "secret") != "Basic bW9vbm1pYzpzZWNyZXQ=") return 3;
    if (!moonmic::sunshineBasicAuth("", "secret").empty()) return 4;
    if (!moonmic::sunshineBasicAuth("moonmic", "").empty()) return 5;

    if (moonmic::sunshineBase64Encode("") != "") return 6;
    if (moonmic::sunshineBase64Encode("f") != "Zg==") return 7;
    if (moonmic::sunshineBase64Encode("fo") != "Zm8=") return 8;
    if (moonmic::sunshineBase64Encode("foo") != "Zm9v") return 9;
    if (moonmic::sunshineBase64Encode("foobar") != "Zm9vYmFy") return 10;

    const std::string binary("\x00\xff\x10", 3);
    if (moonmic::sunshineBase64Encode(binary) != "AP8Q") return 11;

    const auto decoded = moonmic::sunshineBase64Decode("Zm9vYmFy");
    if (!decoded || *decoded != "foobar") return 12;

    const auto decoded_binary = moonmic::sunshineBase64Decode("AP8Q");
    if (!decoded_binary || *decoded_binary != binary) return 13;

    if (moonmic::sunshineBase64Decode("abc")) return 14;
    if (moonmic::sunshineBase64Decode("=m9v")) return 15;
    if (moonmic::sunshineBase64Decode("Zg=A")) return 16;

    using Status = moonmic::SunshineRequestStatus;
    if (moonmic::classifySunshineResponse(true, false, 200) != Status::Success) return 17;
    if (moonmic::classifySunshineResponse(false, true, 0) != Status::EmptyReply) return 18;
    if (moonmic::classifySunshineResponse(false, false, 0) != Status::TransportError) return 19;
    if (moonmic::classifySunshineResponse(true, false, 401) != Status::HttpError) return 20;
    if (moonmic::classifySunshineResponse(true, false, 500) != Status::HttpError) return 21;

    if (!moonmic::sunshineRestartSucceeded(Status::Success)) return 22;
    if (!moonmic::sunshineRestartSucceeded(Status::EmptyReply)) return 23;
    if (moonmic::sunshineRestartSucceeded(Status::TransportError)) return 24;
    if (moonmic::sunshineRestartSucceeded(Status::HttpError)) return 25;

    return 0;
}
