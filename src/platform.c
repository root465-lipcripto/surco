#include "internal.h"

#if SURCO_NASM
#include <cpuid.h>
#endif

static bool cpu_feature(unsigned reg, unsigned mask)
{
#if SURCO_NASM
    unsigned eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return ((reg == 0 ? ecx : edx) & mask) != 0;
#else
    (void)reg;
    (void)mask;
#endif
    return false;
}

bool surco_backend_available(surco_backend backend)
{
    return backend == SURCO_AUTO || backend == SURCO_SCALAR ||
        (backend == SURCO_SSE2 && cpu_feature(1, 1u << 26));
}

bool surco_hardware_crc_available(void)
{
    return cpu_feature(0, 1u << 20);
}

surco_backend surco_resolve_backend(surco_backend backend)
{
    if (backend == SURCO_AUTO)
        return surco_backend_available(SURCO_SSE2) ? SURCO_SSE2 : SURCO_SCALAR;
    return backend;
}

const char *surco_backend_name(surco_backend backend)
{
    switch (backend) {
    case SURCO_AUTO: return "auto";
    case SURCO_SCALAR: return "scalar";
    case SURCO_SSE2: return "sse2";
    default: return "unknown";
    }
}

const char *surco_strerror(surco_status status)
{
    switch (status) {
    case SURCO_OK: return "success";
    case SURCO_END: return "end of records";
    case SURCO_ARGUMENT: return "invalid argument";
    case SURCO_MEMORY: return "allocation failed";
    case SURCO_IO: return "I/O error";
    case SURCO_FORMAT: return "invalid or incomplete format";
    case SURCO_VERSION_ERROR: return "unsupported format version";
    case SURCO_CHECKSUM: return "checksum mismatch";
    case SURCO_ORDER: return "timestamps must be nondecreasing";
    case SURCO_UNSUPPORTED: return "unsupported backend";
    case SURCO_LIMIT: return "size or count limit exceeded";
    default: return "unknown error";
    }
}

surco_ops get_ops(surco_backend backend)
{
#if SURCO_NASM
    if (surco_resolve_backend(backend) == SURCO_SSE2)
        return (surco_ops){surco_delta_sse2, surco_prefix_sse2, surco_hardware_crc_available()};
#else
    (void)backend;
#endif
    return (surco_ops){surco_delta_scalar, surco_prefix_scalar, false};
}

void surco_delta_scalar(uint32_t *out, const uint32_t *in, size_t count)
{
    uint32_t previous = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t current = in[i];
        out[i] = current - previous;
        previous = current;
    }
}

void surco_prefix_scalar(uint32_t *out, const uint32_t *in, size_t count)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += in[i];
        out[i] = sum;
    }
}

uint32_t crc_update(uint32_t state, const uint8_t *p, size_t length, bool hardware)
{
#if SURCO_NASM
    if (hardware)
        return surco_crc_sse42(state, p, length);
#else
    (void)hardware;
#endif
    static const uint32_t table[16] = {
        0x00000000, 0x105ec76f, 0x20bd8ede, 0x30e349b1,
        0x417b1dbc, 0x5125dad3, 0x61c69362, 0x7198540d,
        0x82f63b78, 0x92a8fc17, 0xa24bb5a6, 0xb21572c9,
        0xc38d26c4, 0xd3d3e1ab, 0xe330a81a, 0xf36e6f75
    };
    for (size_t i = 0; i < length; ++i) {
        state ^= p[i];
        state = (state >> 4) ^ table[state & 15];
        state = (state >> 4) ^ table[state & 15];
    }
    return state;
}
