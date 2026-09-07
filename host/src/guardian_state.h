
#pragma once

#include <string>
#include <ctime>

namespace moonmic {

struct GuardianState {
    std::string original_mic_id;
    std::string original_mic_name;
    unsigned long host_pid;
    time_t timestamp;

    GuardianState() : host_pid(0), timestamp(0) {}
};

class GuardianStateManager {
  public:
    static std::string getStatePath();

    static bool writeState(const GuardianState& state);

    static bool readState(GuardianState& state);

    static void deleteState();

    static bool stateExists();
};

} // namespace moonmic
