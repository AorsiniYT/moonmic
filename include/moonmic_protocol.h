#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOONMIC_MAGIC 0x4D4D4943
#define MOONMIC_RAW_FLAG 0x80000000
#define MOONMIC_HEADER_SIZE 20

#define MOONMIC_HANDSHAKE_MAGIC 0x4D4F4F4E
#define MOONMIC_HANDSHAKE_ACK 0x4B434148
#define MOONMIC_PROTOCOL_VERSION 2
#define MOONMIC_FLAG_FORCE_UPDATE 0x01

#define MOONMIC_PING_MAGIC 0x50494E47
#define MOONMIC_PONG_MAGIC 0x504F4E47
#define MOONMIC_CTRL_STOP 0x53544F50
#define MOONMIC_CTRL_START 0x53545254

#define MOONMIC_FOCUS_REQUEST_MAGIC 0x51434F46
#define MOONMIC_FOCUS_RESPONSE_MAGIC 0x52434F46
#define MOONMIC_FOCUS_PROTOCOL_VERSION 1

static inline uint16_t moonmic_read_u16_le(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0]) | ((uint16_t)data[1] << 8));
}

static inline uint32_t moonmic_read_u32_le(const uint8_t* data) {
    return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static inline uint64_t moonmic_read_u64_le(const uint8_t* data) {
    return ((uint64_t)data[0]) | ((uint64_t)data[1] << 8) | ((uint64_t)data[2] << 16) | ((uint64_t)data[3] << 24) |
           ((uint64_t)data[4] << 32) | ((uint64_t)data[5] << 40) | ((uint64_t)data[6] << 48) |
           ((uint64_t)data[7] << 56);
}

static inline void moonmic_write_u16_le(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 0);
    data[1] = (uint8_t)(value >> 8);
}

static inline void moonmic_write_u32_le(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 0);
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static inline void moonmic_write_u64_le(uint8_t* data, uint64_t value) {
    data[0] = (uint8_t)(value >> 0);
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
    data[4] = (uint8_t)(value >> 32);
    data[5] = (uint8_t)(value >> 40);
    data[6] = (uint8_t)(value >> 48);
    data[7] = (uint8_t)(value >> 56);
}

static inline void moonmic_write_packet_header(uint8_t* data, uint32_t sequence, uint64_t timestamp,
                                               uint32_t sample_rate) {
    moonmic_write_u32_le(data, MOONMIC_MAGIC);
    moonmic_write_u32_le(data + 4, sequence);
    moonmic_write_u64_le(data + 8, timestamp);
    moonmic_write_u32_le(data + 16, sample_rate);
}

typedef enum {
    MOONMIC_FOCUS_SOURCE_NONE = 0,
    MOONMIC_FOCUS_SOURCE_CARET = 1,
    MOONMIC_FOCUS_SOURCE_POINTER = 2
} moonmic_focus_source_t;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t pair_status;
    uint8_t uniqueid_len;
    char uniqueid[16];
    uint8_t devicename_len;
    char devicename[64];
    uint16_t display_width;
    uint16_t display_height;
    uint8_t flags;
} moonmic_handshake_t;

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint64_t timestamp;
    uint32_t sample_rate;
} moonmic_packet_header_t;

typedef struct {
    uint32_t magic;
    uint32_t reserved;
} moonmic_control_packet_t;

typedef struct {
    uint32_t magic;
    uint64_t timestamp;
} moonmic_ping_packet_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t pair_status;
    uint8_t uniqueid_len;
    uint8_t reserved;
    char uniqueid[16];
    uint32_t request_id;
    uint32_t reserved_word;
} moonmic_focus_request_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t source;
    uint16_t normalized_x;
    uint16_t normalized_y;
    uint16_t reserved;
    uint32_t request_id;
    uint32_t reserved_word;
} moonmic_focus_response_t;
#pragma pack(pop)

#ifdef __cplusplus
}

static inline void moonmic_read_handshake_le(const uint8_t* data, moonmic_handshake_t* hs) {
    hs->magic = moonmic_read_u32_le(data);
    hs->version = data[4];
    hs->pair_status = data[5];
    hs->uniqueid_len = data[6];
    for (int i = 0; i < 16; ++i) hs->uniqueid[i] = (char)data[7 + i];
    hs->devicename_len = data[23];
    for (int i = 0; i < 64; ++i) hs->devicename[i] = (char)data[24 + i];
    hs->display_width = moonmic_read_u16_le(data + 88);
    hs->display_height = moonmic_read_u16_le(data + 90);
    hs->flags = data[92];
}

static inline void moonmic_write_handshake_le(uint8_t* data, const moonmic_handshake_t* hs) {
    moonmic_write_u32_le(data, hs->magic);
    data[4] = hs->version;
    data[5] = hs->pair_status;
    data[6] = hs->uniqueid_len;
    for (int i = 0; i < 16; ++i) data[7 + i] = (uint8_t)hs->uniqueid[i];
    data[23] = hs->devicename_len;
    for (int i = 0; i < 64; ++i) data[24 + i] = (uint8_t)hs->devicename[i];
    moonmic_write_u16_le(data + 88, hs->display_width);
    moonmic_write_u16_le(data + 90, hs->display_height);
    data[92] = hs->flags;
}

static inline void moonmic_write_ping_le(uint8_t* data, uint32_t magic, uint64_t timestamp) {
    moonmic_write_u32_le(data, magic);
    moonmic_write_u64_le(data + 4, timestamp);
}

static inline uint64_t moonmic_read_ping_timestamp_le(const uint8_t* data) {
    return moonmic_read_u64_le(data + 4);
}

static inline void moonmic_write_control_le(uint8_t* data, uint32_t magic) {
    moonmic_write_u32_le(data, magic);
    moonmic_write_u32_le(data + 4, 0);
}

static inline void moonmic_read_focus_request_le(const uint8_t* data, moonmic_focus_request_t* request) {
    request->magic = moonmic_read_u32_le(data);
    request->version = data[4];
    request->pair_status = data[5];
    request->uniqueid_len = data[6];
    request->reserved = data[7];
    for (int i = 0; i < 16; ++i) request->uniqueid[i] = (char)data[8 + i];
    request->request_id = moonmic_read_u32_le(data + 24);
    request->reserved_word = moonmic_read_u32_le(data + 28);
}

static inline void moonmic_write_focus_response_le(uint8_t* data, const moonmic_focus_response_t* response) {
    moonmic_write_u32_le(data, response->magic);
    data[4] = response->version;
    data[5] = response->source;
    moonmic_write_u16_le(data + 6, response->normalized_x);
    moonmic_write_u16_le(data + 8, response->normalized_y);
    moonmic_write_u16_le(data + 10, response->reserved);
    moonmic_write_u32_le(data + 12, response->request_id);
    moonmic_write_u32_le(data + 16, response->reserved_word);
}

static_assert(sizeof(moonmic_handshake_t) == 93, "Unexpected Moonmic handshake size");
static_assert(sizeof(moonmic_packet_header_t) == MOONMIC_HEADER_SIZE, "Unexpected Moonmic packet header size");
static_assert(sizeof(moonmic_control_packet_t) == 8, "Unexpected Moonmic control packet size");
static_assert(sizeof(moonmic_ping_packet_t) == 12, "Unexpected Moonmic ping packet size");
static_assert(sizeof(moonmic_focus_request_t) == 32, "Unexpected Moonmic focus request size");
static_assert(sizeof(moonmic_focus_response_t) == 20, "Unexpected Moonmic focus response size");
#endif
