#ifndef SURCO_INTERNAL_H
#define SURCO_INTERNAL_H

#include "surco/surco.h"

#include <limits.h>
#include <string.h>

enum {
    HEADER_BASE = 20, HEADER_MAX = HEADER_BASE + SURCO_MAX_CHANNELS * SURCO_NAME_SIZE + 4,
    BLOCK_HEADER = 52, INDEX_ENTRY = 32, FOOTER = 36,
    PAYLOAD_MAX = SURCO_BLOCK_ROWS * (8 + 4 * SURCO_MAX_CHANNELS),
    CODEC_RAW = 0, CODEC_COMPRESSED = 1
};

static inline uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t get64(const uint8_t *p)
{
    return get32(p) | (uint64_t)get32(p + 4) << 32;
}

static inline void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}

static inline void put64(uint8_t *p, uint64_t value)
{
    put32(p, (uint32_t)value);
    put32(p + 4, (uint32_t)(value >> 32));
}

static inline int32_t as_i32(uint32_t value)
{
    if (value <= INT32_MAX)
        return (int32_t)value;
    return -1 - (int32_t)(UINT32_MAX - value);
}

typedef struct {
    uint64_t time[SURCO_BLOCK_ROWS];
    uint32_t channels[SURCO_MAX_CHANNELS][SURCO_BLOCK_ROWS];
} surco_rows;

typedef void (*surco_transform)(uint32_t *out, const uint32_t *in, size_t count);
typedef struct {
    surco_transform delta, prefix;
    bool hardware_crc;
} surco_ops;

void surco_delta_scalar(uint32_t *out, const uint32_t *in, size_t count);
void surco_prefix_scalar(uint32_t *out, const uint32_t *in, size_t count);
#if SURCO_NASM
void surco_delta_sse2(uint32_t *out, const uint32_t *in, size_t count);
void surco_prefix_sse2(uint32_t *out, const uint32_t *in, size_t count);
uint32_t surco_crc_sse42(uint32_t state, const uint8_t *p, size_t length);
#endif
surco_ops get_ops(surco_backend backend);
uint32_t crc_update(uint32_t state, const uint8_t *p, size_t length, bool hardware);
static inline uint32_t crc(const uint8_t *p, size_t length, bool hardware)
{
    return ~crc_update(UINT32_MAX, p, length, hardware);
}

size_t encode_payload(uint8_t *payload, const surco_rows *rows, uint32_t count,
    uint32_t channels, const surco_ops *ops, bool raw_only, uint32_t *codec);
surco_status decode_payload(surco_rows *rows, const uint8_t *payload, size_t size,
    uint32_t count, uint32_t channels, uint32_t codec, uint64_t first, uint64_t last,
    const surco_ops *ops);

#endif
