#ifndef SURCO_H
#define SURCO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SURCO_VERSION "0.1.0"
#define SURCO_MAX_CHANNELS 32
#define SURCO_NAME_SIZE 32
#define SURCO_BLOCK_ROWS 1024

#if defined(__GNUC__) || defined(__clang__)
#define SURCO_API __attribute__((visibility("default")))
#else
#define SURCO_API
#endif

typedef enum {
    SURCO_OK = 0, SURCO_END, SURCO_ARGUMENT, SURCO_MEMORY, SURCO_IO,
    SURCO_FORMAT, SURCO_VERSION_ERROR, SURCO_CHECKSUM, SURCO_ORDER,
    SURCO_UNSUPPORTED, SURCO_LIMIT
} surco_status;

typedef enum { SURCO_AUTO, SURCO_SCALAR, SURCO_SSE2 } surco_backend;
typedef enum { SURCO_US, SURCO_NS, SURCO_MS, SURCO_S } surco_unit;

typedef struct {
    surco_unit unit;
    uint32_t channels;
    char names[SURCO_MAX_CHANNELS][SURCO_NAME_SIZE];
} surco_schema;

typedef struct {
    surco_schema schema;
    uint64_t samples, blocks, raw_blocks, compressed_blocks;
    uint64_t first, last, file_bytes, raw_bytes;
    bool indexed;
} surco_metadata;

typedef struct {
    uint64_t recovered, lost_at_least, accepted_blocks, rejected_blocks;
    bool loss_count_known;
} surco_recovery;

typedef struct surco_encoder surco_encoder;
typedef struct surco_decoder surco_decoder;

/* Streams are borrowed, start at byte zero, and must outlive their context.
 * An indexed encoder needs an empty seekable update stream (w+b).
 * The decoder needs a seekable binary stream. Call finish before destroying
 * an encoder. Errors after writing/reading a block are terminal for that context.
 * A context is single-threaded. Independent streams/contexts can run concurrently. */
SURCO_API const char *surco_strerror(surco_status status);
SURCO_API const char *surco_backend_name(surco_backend backend);
SURCO_API bool surco_backend_available(surco_backend backend);
SURCO_API surco_backend surco_resolve_backend(surco_backend backend);
SURCO_API bool surco_hardware_crc_available(void);
SURCO_API surco_status surco_validate_schema(const surco_schema *schema);
SURCO_API surco_status surco_encoder_create(surco_encoder **out, FILE *stream,
    const surco_schema *schema, surco_backend backend, bool indexed, bool raw_only);
SURCO_API surco_status surco_encoder_push(surco_encoder *ctx, uint64_t timestamp,
    const int32_t *values, size_t count);
SURCO_API surco_status surco_encoder_flush(surco_encoder *ctx);
SURCO_API surco_status surco_encoder_finish(surco_encoder *ctx);
SURCO_API void surco_encoder_destroy(surco_encoder *ctx);

SURCO_API surco_status surco_decoder_create(surco_decoder **out, FILE *stream,
    surco_backend backend);
SURCO_API surco_status surco_decoder_read(surco_decoder *ctx, uint64_t *timestamp,
    int32_t *values, size_t capacity);
/* Inclusive lower bound. END means no matching record. A successful seek leaves
 * the matching record unconsumed. Use read_through for inclusive upper bounds. */
SURCO_API surco_status surco_decoder_seek(surco_decoder *ctx, uint64_t timestamp);
/* Like seek, but skips payload loading if its block starts above upper. */
SURCO_API surco_status surco_decoder_seek_range(surco_decoder *ctx, uint64_t lower, uint64_t upper);
SURCO_API surco_status surco_decoder_read_through(surco_decoder *ctx, uint64_t upper,
    uint64_t *timestamp, int32_t *values, size_t capacity);
SURCO_API void surco_decoder_destroy(surco_decoder *ctx);
SURCO_API surco_status surco_get_metadata(const surco_decoder *ctx, surco_metadata *out);
SURCO_API surco_status surco_verify(FILE *stream, surco_metadata *out, surco_backend backend);
/* Output is a distinct empty stream. Recovery validates each retained block and
 * writes a fresh archive. Verify the completed output before promoting it. */
SURCO_API surco_status surco_recover(FILE *input, FILE *output, bool indexed,
    surco_backend backend, surco_recovery *out);

#ifdef __cplusplus
}
#endif
#endif
