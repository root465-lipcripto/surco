#include "cli.h"
#include "internal.h"

#include <stdlib.h>
#include <time.h>

enum { BENCH_WORDS = 1 << 18, TRANSFORM_PASSES = 32 };

static int compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0)
        return 0;
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static double throughput(surco_transform fn, uint32_t *dst, const uint32_t *src)
{
    const double start = now();
    for (unsigned pass = 0; pass < TRANSFORM_PASSES; ++pass)
        fn(dst, src, BENCH_WORDS);
    const double elapsed = now() - start;
    return elapsed > 0 ? (double)(BENCH_WORDS * sizeof *src * TRANSFORM_PASSES) / (1e6 * elapsed) : 0;
}

int benchmark_transforms(surco_backend backend, unsigned repetitions)
{
    uint32_t *data = malloc(3 * BENCH_WORDS * sizeof *data);
    if (!data)
        return 1;
    uint32_t *delta = data + BENCH_WORDS, *decoded = delta + BENCH_WORDS;
    uint32_t state = 0x6a09e667;
    for (size_t i = 0; i < BENCH_WORDS; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        data[i] = state;
    }

    const surco_ops ops = get_ops(backend);
    double a[31], b[31];
    int result = 1;
    for (unsigned i = 0; i < repetitions; ++i) {
        a[i] = throughput(ops.delta, delta, data);
        b[i] = throughput(ops.prefix, decoded, delta);
        if (a[i] <= 0 || b[i] <= 0 || memcmp(data, decoded, BENCH_WORDS * sizeof *data) != 0)
            goto done;
    }
    qsort(a, repetitions, sizeof *a, compare);
    qsort(b, repetitions, sizeof *b, compare);
    printf("{\"backend\":\"%s\",\"words\":%u,\"passes\":%u,\"repetitions\":%u,"
        "\"delta_mb_s\":{\"min\":%.3f,\"median\":%.3f,\"max\":%.3f},"
        "\"prefix_mb_s\":{\"min\":%.3f,\"median\":%.3f,\"max\":%.3f}}\n",
        surco_backend_name(surco_resolve_backend(backend)), (unsigned)BENCH_WORDS,
        (unsigned)TRANSFORM_PASSES, repetitions,
        a[0], (a[(repetitions - 1) / 2] + a[repetitions / 2]) / 2, a[repetitions - 1],
        b[0], (b[(repetitions - 1) / 2] + b[repetitions / 2]) / 2, b[repetitions - 1]);
    result = 0;
done:
    free(data);
    return result;
}
