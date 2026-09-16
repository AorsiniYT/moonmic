#include "moonmic_protocol.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

int main() {
    std::array<uint8_t, MOONMIC_HEADER_SIZE> header{};
    moonmic_write_packet_header(header.data(), 0xffffffffU, UINT64_MAX, 48000);

    assert(moonmic_read_u32_le(header.data()) == MOONMIC_MAGIC);
    assert(moonmic_read_u32_le(header.data() + 4) == UINT32_MAX);
    assert(moonmic_read_u64_le(header.data() + 8) == UINT64_MAX);
    assert(moonmic_read_u32_le(header.data() + 16) == 48000);

    moonmic_handshake_t hs = {};
    hs.magic = MOONMIC_HANDSHAKE_MAGIC;
    hs.version = MOONMIC_PROTOCOL_VERSION;
    hs.pair_status = 1;
    hs.uniqueid_len = 4;
    std::memcpy(hs.uniqueid, "ab12", 4);
    hs.devicename_len = 7;
    std::memcpy(hs.devicename, "VitaClx", 7);
    hs.display_width = 960;
    hs.display_height = 544;
    hs.flags = MOONMIC_FLAG_FORCE_UPDATE;

    std::array<uint8_t, MOONMIC_HANDSHAKE_SIZE> wire{};
    moonmic_write_handshake_le(wire.data(), &hs);
    assert(moonmic_read_u32_le(wire.data()) == MOONMIC_HANDSHAKE_MAGIC);
    assert(wire[88] == 0xC0 && wire[89] == 0x03);

    moonmic_handshake_t back{};
    assert(moonmic_decode_handshake_le(wire.data(), wire.size(), &back));
    assert(back.magic == hs.magic);
    assert(back.version == hs.version);
    assert(back.pair_status == hs.pair_status);
    assert(back.uniqueid_len == 4 && std::memcmp(back.uniqueid, "ab12", 4) == 0);
    assert(back.devicename_len == 7 && std::memcmp(back.devicename, "VitaClx", 7) == 0);
    assert(back.display_width == 960 && back.display_height == 544);
    assert(back.flags == MOONMIC_FLAG_FORCE_UPDATE);

    std::array<uint8_t, MOONMIC_PING_SIZE> ping{};
    moonmic_write_ping_le(ping.data(), MOONMIC_PING_MAGIC, UINT64_C(0x1122334455667788));
    assert(moonmic_read_u32_le(ping.data()) == MOONMIC_PING_MAGIC);
    assert(moonmic_read_ping_timestamp_le(ping.data()) == UINT64_C(0x1122334455667788));

    assert(!moonmic_decode_handshake_le(wire.data(), wire.size() - 1, &back));

    auto malformed = wire;
    malformed[6] = MOONMIC_UNIQUE_ID_CAPACITY + 1;
    assert(!moonmic_decode_handshake_le(malformed.data(), malformed.size(), &back));
    malformed = wire;
    malformed[23] = MOONMIC_DEVICE_NAME_CAPACITY + 1;
    assert(!moonmic_decode_handshake_le(malformed.data(), malformed.size(), &back));

    std::array<uint8_t, MOONMIC_HANDSHAKE_SIZE> junk{};
    junk.fill(0xFF);
    moonmic_handshake_t junk_hs{};
    assert(!moonmic_decode_handshake_le(junk.data(), junk.size(), &junk_hs));

    std::array<uint8_t, MOONMIC_HANDSHAKE_SIZE> zeros{};
    moonmic_handshake_t zero_hs{};
    assert(!moonmic_decode_handshake_le(zeros.data(), zeros.size(), &zero_hs));

    std::array<uint8_t, MOONMIC_HEADER_SIZE> wrong_magic{};
    assert(moonmic_read_u32_le(wrong_magic.data()) != MOONMIC_MAGIC);
    return 0;
}
