#pragma once

#include <cstdint>

namespace moonmic {

struct TypingFocus {
    std::uint16_t normalized_x = 32768;
    std::uint16_t normalized_y = 32768;
    std::uint8_t source = 0;
};

bool getTypingFocus(TypingFocus& focus);

} // namespace moonmic
