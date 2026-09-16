#include "network/udp_receiver.h"

int main() {
    moonmic::UDPReceiver receiver;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!receiver.start(0, "127.0.0.1")) return 1;
        if (!receiver.isRunning()) return 2;
        if (receiver.start(0, "127.0.0.1")) return 3;
        receiver.stop();
        if (receiver.isRunning()) return 4;
    }

    receiver.stop();
    return receiver.start(48100, "not-an-ip") ? 5 : 0;
}
