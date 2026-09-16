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
    return 0;
}
