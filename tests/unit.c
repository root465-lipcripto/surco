#include "internal.h"

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

#if SURCO_NASM
int check_abi(surco_transform fn, uint32_t *out, const uint32_t *in, size_t count);
#endif

static uint32_t random_word(void)
{
    static uint32_t value = 0x9e3779b9;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static void check_transforms(surco_ops ops, bool simd)
{
    uint32_t expected[256], original[256], result[258];
    for (size_t n = 0; n <= 256; ++n) {
        for (size_t i = 0; i < n; ++i)
            original[i] = i == 0 ? UINT32_C(0x80000000) : i == 1 ? UINT32_C(0x7fffffff) : random_word();
        surco_delta_scalar(expected, original, n);
        for (size_t offset = 0; offset < 2; ++offset) {
            memset(result, 0x5a, sizeof result);
            ops.delta(result + offset, original, n);
            CHECK(memcmp(result + offset, expected, n * 4) == 0);
            CHECK(result[offset + n] == UINT32_C(0x5a5a5a5a));
            if (offset)
                CHECK(result[0] == UINT32_C(0x5a5a5a5a));
            ops.prefix(result + offset, result + offset, n);
            CHECK(memcmp(result + offset, original, n * 4) == 0);
            ops.delta(result + offset, result + offset, n);
            CHECK(memcmp(result + offset, expected, n * 4) == 0);
        }
#if SURCO_NASM
        if (simd) {
            CHECK(check_abi(ops.delta, result, original, n));
            CHECK(check_abi(ops.prefix, result, expected, n));
        }
#else
        (void)simd;
#endif
    }
}

static void check_guards(surco_ops ops)
{
    const long page = sysconf(_SC_PAGESIZE);
    CHECK(page >= 4096);
    const size_t p = (size_t)page;
    uint8_t *map = mmap(NULL, 6 * p, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(map != MAP_FAILED);
    CHECK(mprotect(map + p, p, PROT_READ | PROT_WRITE) == 0);
    CHECK(mprotect(map + 4 * p, p, PROT_READ | PROT_WRITE) == 0);
    for (size_t n = 0; n <= p / 4; ++n) {
        uint32_t *src = (uint32_t *)(map + 2 * p - n * 4);
        uint32_t *dst = (uint32_t *)(map + 5 * p - n * 4);
        for (size_t i = 0; i < n; ++i)
            src[i] = random_word();
        ops.delta(dst, src, n);
        ops.prefix(dst, dst, n);
        CHECK(memcmp(dst, src, n * 4) == 0);
        CHECK(crc((uint8_t *)src, n * 4, ops.hardware_crc) == crc((uint8_t *)src, n * 4, false));
    }

    uint32_t *src = (uint32_t *)(map + p);
    uint32_t *dst = (uint32_t *)(map + 4 * p);
    for (size_t n = 1; n <= 17; ++n) {
        ops.delta(dst, src, n);
        ops.prefix(dst, dst, n);
        CHECK(memcmp(dst, src, n * 4) == 0);
    }
    CHECK(munmap(map, 6 * p) == 0);
}

static void check_public(surco_backend backend)
{
    /* Anonymous in-memory stdio avoids placement in a system temp directory. */
    char *archive = NULL;
    size_t bytes = 0;
    FILE *out = open_memstream(&archive, &bytes);
    CHECK(out);
    surco_schema schema = {.unit = SURCO_NS, .channels = 1, .names = {"signal"}};
    surco_encoder *writer;
    CHECK(surco_encoder_create(&writer, out, &schema, backend, false, false) == SURCO_OK);
    const int32_t value = INT32_MIN;
    CHECK(surco_encoder_push(writer, 9, &value, 0) == SURCO_ARGUMENT);
    CHECK(surco_encoder_push(writer, 9, &value, 1) == SURCO_OK);
    CHECK(surco_encoder_flush(writer) == SURCO_OK);
    CHECK(surco_encoder_push(writer, 8, &value, 1) == SURCO_ORDER);
    CHECK(surco_encoder_push(writer, UINT64_MAX, &value, 1) == SURCO_OK);
    CHECK(surco_encoder_finish(writer) == SURCO_OK);
    CHECK(surco_encoder_finish(writer) == SURCO_ARGUMENT);
    surco_encoder_destroy(writer);
    CHECK(fclose(out) == 0);
    FILE *in = fmemopen(archive, bytes, "rb");
    CHECK(in);
    surco_decoder *reader;
    CHECK(surco_decoder_create(&reader, in, backend) == SURCO_OK);
    CHECK(surco_decoder_seek(reader, 8) == SURCO_OK);
    uint64_t time;
    int32_t sample;
    CHECK(surco_decoder_read(reader, &time, &sample, 0) == SURCO_ARGUMENT);
    CHECK(surco_decoder_read(reader, &time, &sample, 1) == SURCO_OK && time == 9 && sample == value);
    CHECK(surco_decoder_read_through(reader, 10, &time, &sample, 1) == SURCO_END);
    CHECK(surco_decoder_read(reader, &time, &sample, 1) == SURCO_OK && time == UINT64_MAX);
    CHECK(surco_decoder_read(reader, &time, &sample, 1) == SURCO_END);
    CHECK(surco_decoder_seek(reader, 10) == SURCO_OK);
    CHECK(surco_decoder_read(reader, &time, &sample, 1) == SURCO_OK && time == UINT64_MAX);
    surco_decoder_destroy(reader);
    fclose(in);
    free(archive);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const surco_backend backend = strcmp(argv[1], "sse2") == 0 ? SURCO_SSE2 : SURCO_SCALAR;
    if (!surco_backend_available(backend)) {
        puts("SSE2 implementation or CPU support is unavailable");
        return 77;
    }
    const surco_ops ops = get_ops(backend);
    const uint8_t text[] = "123456789";
    CHECK(crc(text, sizeof text - 1, false) == UINT32_C(0xe3069283));
    CHECK(crc(text, sizeof text - 1, ops.hardware_crc) == UINT32_C(0xe3069283));
    check_transforms(ops, backend == SURCO_SSE2);
    check_guards(ops);
    check_public(backend);
    return 0;
}
