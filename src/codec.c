#include "internal.h"

static uint64_t zigzag64(uint64_t value)
{
    return (value << 1) ^ (UINT64_C(0) - (value >> 63));
}

static uint64_t unzigzag64(uint64_t value)
{
    return (value >> 1) ^ (UINT64_C(0) - (value & 1));
}

static uint32_t zigzag32(uint32_t value)
{
    return (value << 1) ^ (UINT32_C(0) - (value >> 31));
}

static uint32_t unzigzag32(uint32_t value)
{
    return (value >> 1) ^ (UINT32_C(0) - (value & 1));
}

static uint8_t *put_var(uint8_t *p, uint64_t value)
{
    while (value >= 128) {
        *p++ = (uint8_t)((value & 127) | 128);
        value >>= 7;
    }
    *p++ = (uint8_t)value;
    return p;
}

static bool get_var(const uint8_t **cursor, const uint8_t *end, uint64_t *value)
{
    uint64_t result = 0;
    for (unsigned shift = 0; shift <= 63 && *cursor != end; shift += 7) {
        uint8_t byte = *(*cursor)++;
        if (shift == 63 && byte > 1)
            return false;
        result |= (uint64_t)(byte & 127) << shift;
        if (byte < 128) {
            *value = result;
            return shift == 0 || byte != 0;
        }
    }
    return false;
}

static uint8_t *pack_bits(uint8_t *p, const uint32_t *values, uint32_t count, unsigned width)
{
    uint64_t acc = 0;
    unsigned bits = 0;
    for (uint32_t i = 0; i < count; ++i) {
        acc |= (uint64_t)values[i] << bits;
        bits += width;
        while (bits >= 8) {
            *p++ = (uint8_t)acc;
            acc >>= 8;
            bits -= 8;
        }
    }
    if (bits != 0)
        *p++ = (uint8_t)acc;
    return p;
}

static bool unpack_bits(const uint8_t **p, const uint8_t *end, uint32_t *values,
    uint32_t count, unsigned width)
{
    const size_t bytes = ((size_t)count * width + 7) / 8;
    if ((size_t)(end - *p) < bytes)
        return false;

    const uint64_t mask = (UINT64_C(1) << width) - 1;
    uint64_t acc = 0;
    unsigned bits = 0;
    for (uint32_t i = 0; i < count; ++i) {
        while (bits < width) {
            acc |= (uint64_t)*(*p)++ << bits;
            bits += 8;
        }
        values[i] = unzigzag32((uint32_t)(acc & mask));
        acc >>= width;
        bits -= width;
    }
    return acc == 0;
}

static uint8_t *encode_time(uint8_t *p, const surco_rows *rows, uint32_t count)
{
    uint8_t delta[SURCO_BLOCK_ROWS * 10], dod[SURCO_BLOCK_ROWS * 10];
    uint8_t *a = delta, *b = dod;
    uint64_t previous = 0;
    for (uint32_t i = 1; i < count; ++i) {
        uint64_t gap = rows->time[i] - rows->time[i - 1];
        a = put_var(a, gap);
        b = put_var(b, i == 1 ? gap : zigzag64(gap - previous));
        previous = gap;
    }

    const bool use_dod = (b - dod) < (a - delta);
    const size_t var_size = use_dod ? (size_t)(b - dod) : (size_t)(a - delta);
    if (var_size <= (size_t)(count - 1) * 8) {
        *p++ = use_dod ? 2 : 1;
        memcpy(p, use_dod ? dod : delta, var_size);
        return p + var_size;
    }
    *p++ = 0;
    for (uint32_t i = 1; i < count; ++i, p += 8)
        put64(p, rows->time[i]);
    return p;
}

static uint8_t *encode_channel(uint8_t *p, const uint32_t *values, uint32_t count,
    surco_transform delta)
{
    uint32_t words[SURCO_BLOCK_ROWS];
    uint8_t runs[SURCO_BLOCK_ROWS * 7];
    uint8_t *r = runs;
    delta(words, values, count);
    uint32_t used = 0;
    for (uint32_t i = 1; i < count; ++i) {
        words[i] = zigzag32(words[i]);
        used |= words[i];
    }
    unsigned width = 0;
    for (; used != 0; used >>= 1)
        ++width;

    for (uint32_t i = 1; i < count;) {
        uint32_t end = i + 1;
        while (end < count && words[end] == words[i])
            ++end;
        r = put_var(r, end - i);
        r = put_var(r, words[i]);
        i = end;
    }

    const size_t run_size = (size_t)(r - runs);
    const bool use_runs = run_size < ((size_t)(count - 1) * width + 7) / 8;
    *p++ = use_runs ? 33 : (uint8_t)width;
    put32(p, words[0]);
    p += 4;
    if (use_runs) {
        memcpy(p, runs, run_size);
        return p + run_size;
    }
    return pack_bits(p, words + 1, count - 1, width);
}

size_t encode_payload(uint8_t *payload, const surco_rows *rows, uint32_t count,
    uint32_t channels, const surco_ops *ops, bool raw_only, uint32_t *codec)
{
    const size_t raw_size = (size_t)count * (8 + 4 * channels);
    if (!raw_only) {
        /* Each stream has at most one extra mode byte over its raw size. */
        uint8_t candidate[PAYLOAD_MAX + SURCO_MAX_CHANNELS + 1];
        uint8_t *p = encode_time(candidate, rows, count);
        for (uint32_t c = 0; c < channels; ++c)
            p = encode_channel(p, rows->channels[c], count, ops->delta);
        const size_t size = (size_t)(p - candidate);
        if (size < raw_size) {
            memcpy(payload, candidate, size);
            *codec = CODEC_COMPRESSED;
            return size;
        }
    }

    uint8_t *p = payload;
    for (uint32_t i = 0; i < count; ++i) {
        put64(p, rows->time[i]);
        p += 8;
        for (uint32_t c = 0; c < channels; ++c, p += 4)
            put32(p, rows->channels[c][i]);
    }
    *codec = CODEC_RAW;
    return raw_size;
}

static bool decode_time(uint64_t *time, uint32_t count, uint64_t first,
    const uint8_t **p, const uint8_t *end)
{
    if (*p == end)
        return false;
    const uint8_t mode = *(*p)++;
    if (mode > 2)
        return false;

    time[0] = first;
    uint64_t gap = 0;
    for (uint32_t i = 1; i < count; ++i) {
        if (mode == 0) {
            if ((size_t)(end - *p) < 8)
                return false;
            time[i] = get64(*p);
            *p += 8;
        } else {
            uint64_t code;
            if (!get_var(p, end, &code))
                return false;
            gap = mode == 2 && i > 1 ? gap + unzigzag64(code) : code;
            if (gap > UINT64_MAX - time[i - 1])
                return false;
            time[i] = time[i - 1] + gap;
        }
    }
    return true;
}

static bool decode_channel(uint32_t *values, uint32_t count, const uint8_t **p,
    const uint8_t *end, surco_transform prefix)
{
    if ((size_t)(end - *p) < 5)
        return false;
    const unsigned mode = *(*p)++;
    values[0] = get32(*p);
    *p += 4;
    if (mode == 33) {
        uint32_t i = 1;
        while (i < count) {
            uint64_t run, code;
            if (!get_var(p, end, &run) || run == 0 || run > count - i ||
                !get_var(p, end, &code) || code > UINT32_MAX)
                return false;
            const uint32_t value = unzigzag32((uint32_t)code);
            for (uint64_t j = 0; j < run; ++j)
                values[i++] = value;
        }
    } else if (mode > 32 || !unpack_bits(p, end, values + 1, count - 1, mode)) {
        return false;
    }
    prefix(values, values, count);
    return true;
}

surco_status decode_payload(surco_rows *rows, const uint8_t *payload, size_t size,
    uint32_t count, uint32_t channels, uint32_t codec, uint64_t first, uint64_t last,
    const surco_ops *ops)
{
    const uint8_t *p = payload;
    const uint8_t *end = payload + size;
    if (codec == CODEC_RAW) {
        if (size != (size_t)count * (8 + 4 * channels))
            return SURCO_FORMAT;
        for (uint32_t i = 0; i < count; ++i) {
            rows->time[i] = get64(p);
            p += 8;
            for (uint32_t c = 0; c < channels; ++c, p += 4)
                rows->channels[c][i] = get32(p);
        }
    } else {
        if (!decode_time(rows->time, count, first, &p, end))
            return SURCO_FORMAT;
        for (uint32_t c = 0; c < channels; ++c)
            if (!decode_channel(rows->channels[c], count, &p, end, ops->prefix))
                return SURCO_FORMAT;
    }
    if (p != end || rows->time[0] != first || rows->time[count - 1] != last)
        return SURCO_FORMAT;
    for (uint32_t i = 1; i < count; ++i)
        if (rows->time[i] < rows->time[i - 1])
            return SURCO_ORDER;
    return SURCO_OK;
}
