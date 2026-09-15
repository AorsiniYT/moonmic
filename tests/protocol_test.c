#include "moonmic_protocol.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

int main(void) {
    static const uint8_t expected[MOONMIC_HEADER_SIZE] = {
        0x43, 0x49, 0x4d, 0x4d, 0x04, 0x03, 0x02, 0x01, 0x08, 0x07,
        0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x80, 0x3e, 0x00, 0x80,
    };
    uint8_t header[MOONMIC_HEADER_SIZE] = {0};

    moonmic_write_packet_header(header, 0x01020304, UINT64_C(0x0102030405060708), MOONMIC_RAW_FLAG | 16000);

    assert(memcmp(header, expected, sizeof(expected)) == 0);
    assert(moonmic_read_u32_le(header) == MOONMIC_MAGIC);
    assert(moonmic_read_u32_le(header + 4) == UINT32_C(0x01020304));
    assert(moonmic_read_u64_le(header + 8) == UINT64_C(0x0102030405060708));
    assert(moonmic_read_u32_le(header + 16) == (MOONMIC_RAW_FLAG | 16000));

    moonmic_write_u16_le(header, UINT16_C(0x8123));
    assert(header[0] == 0x23 && header[1] == 0x81);
    assert(moonmic_read_u16_le(header) == UINT16_C(0x8123));
    return 0;
}
