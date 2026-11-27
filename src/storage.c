#include "internal.h"

#include <stdlib.h>
#include <sys/types.h>

static const uint8_t signature[] = {'S','U','R','C','O','\r','\n',0x1a};

typedef struct {
    uint64_t sequence, first, last;
    uint32_t count, codec, size, checksum;
} block;

typedef struct {
    uint64_t offset, blocks, samples;
    uint32_t checksum;
} footer;

struct surco_encoder {
    FILE *file;
    surco_schema schema;
    surco_ops ops;
    uint64_t offset, header_size, blocks, samples, last;
    uint32_t count;
    bool indexed, raw_only, failed, finished;
    surco_rows rows;
    uint8_t payload[PAYLOAD_MAX];
};

struct surco_decoder {
    FILE *file;
    surco_ops ops;
    surco_metadata meta;
    uint64_t header_size, data_end, next, sequence;
    uint32_t count, row;
    bool failed;
    surco_rows rows;
    uint8_t payload[PAYLOAD_MAX];
};

static bool name_char(unsigned char c, bool first)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
        (!first && c >= '0' && c <= '9');
}

surco_status surco_validate_schema(const surco_schema *schema)
{
    if (!schema || schema->channels < 1 || schema->channels > SURCO_MAX_CHANNELS ||
        (unsigned)schema->unit > SURCO_S)
        return SURCO_ARGUMENT;

    for (uint32_t i = 0; i < schema->channels; ++i) {
        const char *name = schema->names[i];
        const char *end = memchr(name, 0, SURCO_NAME_SIZE);
        if (!end || name == end)
            return SURCO_ARGUMENT;
        for (const char *p = name; p < end; ++p)
            if (!name_char((unsigned char)*p, p == name))
                return SURCO_ARGUMENT;
        for (uint32_t j = 0; j < i; ++j)
            if (strcmp(schema->names[j], name) == 0)
                return SURCO_ARGUMENT;
    }
    return SURCO_OK;
}

static surco_status read_exact(FILE *file, void *p, size_t n)
{
    if (fread(p, 1, n, file) == n)
        return SURCO_OK;
    return ferror(file) ? SURCO_IO : SURCO_FORMAT;
}

static surco_status seek_file(FILE *file, uint64_t offset)
{
    if (offset > INT64_MAX)
        return SURCO_LIMIT;
    return fseeko(file, (off_t)offset, SEEK_SET) == 0 ? SURCO_OK : SURCO_IO;
}

static surco_status read_at(FILE *file, uint64_t offset, void *p, size_t n)
{
    surco_status status = seek_file(file, offset);
    return status == SURCO_OK ? read_exact(file, p, n) : status;
}

static uint64_t header_size(uint32_t channels)
{
    return HEADER_BASE + channels * SURCO_NAME_SIZE + 4;
}

static surco_status write_header(FILE *file, const surco_schema *schema, bool indexed, bool hw)
{
    uint8_t h[HEADER_MAX] = {0};
    const size_t size = (size_t)header_size(schema->channels);
    memcpy(h, signature, sizeof signature);
    h[8] = 1;
    put32(h + 12, (uint32_t)size);
    h[16] = indexed;
    h[17] = (uint8_t)schema->channels;
    h[18] = (uint8_t)schema->unit;
    for (uint32_t i = 0; i < schema->channels; ++i)
        memcpy(h + HEADER_BASE + i * SURCO_NAME_SIZE, schema->names[i], strlen(schema->names[i]));
    put32(h + size - 4, crc(h, size - 4, hw));
    return fwrite(h, 1, size, file) == size ? SURCO_OK : SURCO_IO;
}

static surco_status read_header(FILE *file, surco_schema *schema, bool *indexed, bool hw)
{
    uint8_t h[HEADER_MAX];
    surco_status status = read_at(file, 0, h, HEADER_BASE);
    if (status != SURCO_OK)
        return status;
    if (memcmp(h, signature, sizeof signature) != 0)
        return SURCO_FORMAT;
    if (get32(h + 8) != 1)
        return SURCO_VERSION_ERROR;
    if (h[16] > 1 || !h[17] || h[17] > SURCO_MAX_CHANNELS || h[18] > SURCO_S || h[19] != 0)
        return SURCO_FORMAT;
    const size_t size = (size_t)header_size(h[17]);
    if (get32(h + 12) != size)
        return SURCO_FORMAT;
    status = read_exact(file, h + HEADER_BASE, size - HEADER_BASE);
    if (status != SURCO_OK)
        return status;
    if (get32(h + size - 4) != crc(h, size - 4, hw))
        return SURCO_CHECKSUM;

    *schema = (surco_schema){.unit = (surco_unit)h[18], .channels = h[17]};
    *indexed = h[16] != 0;
    for (uint32_t i = 0; i < schema->channels; ++i) {
        const uint8_t *name = h + HEADER_BASE + i * SURCO_NAME_SIZE;
        const uint8_t *end = memchr(name, 0, SURCO_NAME_SIZE);
        if (!end)
            return SURCO_FORMAT;
        for (const uint8_t *p = end; p != name + SURCO_NAME_SIZE; ++p)
            if (*p != 0)
                return SURCO_FORMAT;
        memcpy(schema->names[i], name, SURCO_NAME_SIZE);
    }
    return surco_validate_schema(schema) == SURCO_OK ? SURCO_OK : SURCO_FORMAT;
}

static surco_status read_block(FILE *file, uint64_t offset, uint64_t end, uint32_t channels,
    bool hw, block *out)
{
    if (offset > end || end - offset < BLOCK_HEADER)
        return SURCO_FORMAT;
    uint8_t bytes[BLOCK_HEADER];
    surco_status status = read_at(file, offset, bytes, sizeof bytes);
    if (status != SURCO_OK)
        return status;
    if (memcmp(bytes, "BLK1", 4) != 0)
        return SURCO_FORMAT;
    if (get32(bytes + 48) != crc(bytes, 48, hw))
        return SURCO_CHECKSUM;

    *out = (block){.codec = get32(bytes + 4), .sequence = get64(bytes + 8),
        .count = get32(bytes + 16), .size = get32(bytes + 20), .checksum = get32(bytes + 28),
        .first = get64(bytes + 32), .last = get64(bytes + 40)};
    if (!out->count || out->count > SURCO_BLOCK_ROWS || out->codec > CODEC_COMPRESSED ||
        out->first > out->last || out->size > end - offset - BLOCK_HEADER)
        return SURCO_FORMAT;
    const uint32_t raw = out->count * (8 + 4 * channels);
    if (get32(bytes + 24) != raw ||
        (out->codec == CODEC_RAW && out->size != raw) ||
        (out->codec == CODEC_COMPRESSED && (out->size < 1 + channels * 5 || out->size >= raw)))
        return SURCO_FORMAT;
    return SURCO_OK;
}

static surco_status read_payload(FILE *file, const block *b, uint32_t channels,
    const surco_ops *ops, uint8_t *payload, surco_rows *rows)
{
    surco_status s = read_exact(file, payload, b->size);
    if (s != SURCO_OK)
        return s;
    if (crc(payload, b->size, ops->hardware_crc) != b->checksum)
        return SURCO_CHECKSUM;
    return decode_payload(rows, payload, b->size, b->count, channels,
        b->codec, b->first, b->last, ops);
}

static void make_entry(uint8_t *entry, uint64_t offset, const block *b)
{
    put64(entry, offset);
    put64(entry + 8, b->first);
    put64(entry + 16, b->last);
    put32(entry + 24, b->count);
    put32(entry + 28, BLOCK_HEADER + b->size);
}

static surco_status read_footer(FILE *file, uint64_t size, uint64_t min, bool indexed,
    bool hw, footer *out)
{
    if (size < min || size - min < FOOTER)
        return SURCO_FORMAT;
    uint8_t bytes[FOOTER];
    surco_status status = read_at(file, size - FOOTER, bytes, sizeof bytes);
    if (status != SURCO_OK)
        return status;
    if (memcmp(bytes, "END1", 4) != 0)
        return SURCO_FORMAT;
    if (get32(bytes + 32) != crc(bytes, 32, hw))
        return SURCO_CHECKSUM;
    *out = (footer){get64(bytes + 4), get64(bytes + 12), get64(bytes + 20), get32(bytes + 28)};
    if (out->offset < min || out->offset > size - FOOTER)
        return SURCO_FORMAT;
    const uint64_t length = size - FOOTER - out->offset;
    if (length % INDEX_ENTRY || out->blocks != length / INDEX_ENTRY ||
        (!indexed && (out->blocks != 0 || out->checksum != 0)))
        return SURCO_FORMAT;
    return SURCO_OK;
}

static surco_status file_size(FILE *file, uint64_t *size)
{
    if (fseeko(file, 0, SEEK_END) != 0)
        return SURCO_IO;
    const off_t end = ftello(file);
    if (end < 0)
        return SURCO_IO;
    *size = (uint64_t)end;
    return SURCO_OK;
}

surco_status surco_encoder_create(surco_encoder **out, FILE *stream,
    const surco_schema *schema, surco_backend backend, bool indexed, bool raw_only)
{
    if (!out)
        return SURCO_ARGUMENT;
    *out = NULL;
    if (!stream || surco_validate_schema(schema) != SURCO_OK)
        return SURCO_ARGUMENT;
    if (!surco_backend_available(backend))
        return SURCO_UNSUPPORTED;
    if (indexed) {
        uint64_t size;
        surco_status status = file_size(stream, &size);
        if (status != SURCO_OK)
            return status;
        if (size != 0)
            return SURCO_ARGUMENT;
    }
    surco_encoder *ctx = calloc(1, sizeof *ctx);
    if (!ctx)
        return SURCO_MEMORY;
    ctx->file = stream;
    ctx->schema = *schema;
    ctx->ops = get_ops(backend);
    ctx->indexed = indexed;
    ctx->raw_only = raw_only;
    ctx->header_size = ctx->offset = header_size(schema->channels);
    const surco_status status = write_header(stream, schema, indexed, ctx->ops.hardware_crc);
    if (status != SURCO_OK) {
        free(ctx);
        return status;
    }
    *out = ctx;
    return SURCO_OK;
}

surco_status surco_encoder_push(surco_encoder *ctx, uint64_t timestamp,
    const int32_t *values, size_t count)
{
    if (!ctx || !values || count != ctx->schema.channels || ctx->finished || ctx->failed)
        return SURCO_ARGUMENT;
    if (ctx->samples != 0 && timestamp < ctx->last)
        return SURCO_ORDER;
    if (ctx->samples == UINT64_MAX)
        return SURCO_LIMIT;
    const uint32_t row = ctx->count++;
    ctx->rows.time[row] = ctx->last = timestamp;
    for (uint32_t c = 0; c < ctx->schema.channels; ++c)
        ctx->rows.channels[c][row] = (uint32_t)values[c];
    ++ctx->samples;
    return ctx->count == SURCO_BLOCK_ROWS ? surco_encoder_flush(ctx) : SURCO_OK;
}

surco_status surco_encoder_flush(surco_encoder *ctx)
{
    if (!ctx || ctx->finished || ctx->failed)
        return SURCO_ARGUMENT;
    if (!ctx->count)
        return SURCO_OK;
    uint32_t codec;
    const size_t size = encode_payload(ctx->payload, &ctx->rows, ctx->count, ctx->schema.channels,
        &ctx->ops, ctx->raw_only, &codec);
    if ((uint64_t)INT64_MAX - ctx->offset < BLOCK_HEADER + size) {
        ctx->failed = true;
        return SURCO_LIMIT;
    }

    uint8_t header[BLOCK_HEADER];
    memcpy(header, "BLK1", 4);
    put32(header + 4, codec);
    put64(header + 8, ctx->blocks);
    put32(header + 16, ctx->count);
    put32(header + 20, (uint32_t)size);
    put32(header + 24, ctx->count * (8 + 4 * ctx->schema.channels));
    put32(header + 28, crc(ctx->payload, size, ctx->ops.hardware_crc));
    put64(header + 32, ctx->rows.time[0]);
    put64(header + 40, ctx->last);
    put32(header + 48, crc(header, 48, ctx->ops.hardware_crc));
    if (fwrite(header, 1, sizeof header, ctx->file) != sizeof header ||
        fwrite(ctx->payload, 1, size, ctx->file) != size) {
        ctx->failed = true;
        return SURCO_IO;
    }

    ctx->offset += BLOCK_HEADER + size;
    ++ctx->blocks;
    ctx->count = 0;
    return SURCO_OK;
}

static surco_status write_index(surco_encoder *ctx, uint32_t *checksum)
{
    /* Read header batches from the update stream, then append their entries. */
    uint64_t offset = ctx->header_size, dest = ctx->offset;
    uint32_t state = UINT32_MAX;
    for (uint64_t start = 0; start < ctx->blocks;) {
        uint8_t batch[64 * INDEX_ENTRY];
        size_t used = 0;
        while (start < ctx->blocks && used < sizeof batch) {
            block info;
            surco_status status = read_block(ctx->file, offset, ctx->offset, ctx->schema.channels,
                ctx->ops.hardware_crc, &info);
            if (status != SURCO_OK)
                return status;
            make_entry(batch + used, offset, &info);
            offset += BLOCK_HEADER + info.size;
            used += INDEX_ENTRY;
            ++start;
        }
        surco_status status = seek_file(ctx->file, dest);
        if (status != SURCO_OK)
            return status;
        if (fwrite(batch, 1, used, ctx->file) != used)
            return SURCO_IO;
        state = crc_update(state, batch, used, ctx->ops.hardware_crc);
        dest += used;
    }
    *checksum = ~state;
    return SURCO_OK;
}

surco_status surco_encoder_finish(surco_encoder *ctx)
{
    surco_status status = surco_encoder_flush(ctx);
    if (status != SURCO_OK)
        return status;
    const uint64_t entries = ctx->indexed ? ctx->blocks : 0;
    uint32_t checksum = 0;
    const uint64_t space = (uint64_t)INT64_MAX - ctx->offset;
    if (space < FOOTER || entries > (space - FOOTER) / INDEX_ENTRY)
        status = SURCO_LIMIT;
    else if (ctx->indexed)
        status = write_index(ctx, &checksum);
    if (status != SURCO_OK) {
        ctx->failed = true;
        return status;
    }

    uint8_t bytes[FOOTER];
    memcpy(bytes, "END1", 4);
    put64(bytes + 4, ctx->offset);
    put64(bytes + 12, entries);
    put64(bytes + 20, ctx->samples);
    put32(bytes + 28, checksum);
    put32(bytes + 32, crc(bytes, 32, ctx->ops.hardware_crc));
    if (fwrite(bytes, 1, sizeof bytes, ctx->file) != sizeof bytes || fflush(ctx->file) != 0) {
        ctx->failed = true;
        return SURCO_IO;
    }
    ctx->finished = true;
    return SURCO_OK;
}

void surco_encoder_destroy(surco_encoder *ctx)
{
    free(ctx);
}

static surco_status validate_layout(surco_decoder *ctx, const footer *end)
{
    surco_metadata *meta = &ctx->meta;
    const bool hw = ctx->ops.hardware_crc;
    uint32_t state = UINT32_MAX;
    uint64_t offset = ctx->header_size;
    while (offset < end->offset) {
        block b;
        surco_status status = read_block(ctx->file, offset, end->offset, meta->schema.channels, hw, &b);
        if (status != SURCO_OK)
            return status;
        if (b.sequence != meta->blocks || (meta->blocks && b.first < meta->last))
            return SURCO_FORMAT;
        if (meta->indexed) {
            if (meta->blocks >= end->blocks)
                return SURCO_FORMAT;
            uint8_t actual[INDEX_ENTRY], expected[INDEX_ENTRY];
            status = read_at(ctx->file, end->offset + meta->blocks * INDEX_ENTRY, actual, sizeof actual);
            if (status != SURCO_OK)
                return status;
            make_entry(expected, offset, &b);
            if (memcmp(actual, expected, sizeof actual) != 0)
                return SURCO_FORMAT;
            state = crc_update(state, actual, sizeof actual, hw);
        }
        if (meta->blocks == 0)
            meta->first = b.first;
        meta->last = b.last;
        ++meta->blocks;
        if (b.codec == CODEC_RAW)
            ++meta->raw_blocks;
        else
            ++meta->compressed_blocks;
        if (meta->samples > UINT64_MAX - b.count)
            return SURCO_LIMIT;
        meta->samples += b.count;
        offset += BLOCK_HEADER + b.size;
    }
    const uint64_t row_bytes = 8 + 4 * meta->schema.channels;
    if (meta->samples > UINT64_MAX / row_bytes)
        return SURCO_LIMIT;
    meta->raw_bytes = meta->samples * row_bytes;
    if (meta->samples != end->samples || (meta->indexed && meta->blocks != end->blocks))
        return SURCO_FORMAT;
    return ~state == end->checksum ? SURCO_OK : SURCO_CHECKSUM;
}

surco_status surco_decoder_create(surco_decoder **out, FILE *stream, surco_backend backend)
{
    if (!out)
        return SURCO_ARGUMENT;
    *out = NULL;
    if (!stream)
        return SURCO_ARGUMENT;
    if (!surco_backend_available(backend))
        return SURCO_UNSUPPORTED;
    surco_decoder *ctx = calloc(1, sizeof *ctx);
    if (!ctx)
        return SURCO_MEMORY;
    ctx->file = stream;
    ctx->ops = get_ops(backend);
    const bool hw = ctx->ops.hardware_crc;
    surco_status status = read_header(stream, &ctx->meta.schema, &ctx->meta.indexed, hw);
    if (status == SURCO_OK)
        status = file_size(stream, &ctx->meta.file_bytes);
    ctx->header_size = ctx->next = header_size(ctx->meta.schema.channels);
    footer end;
    if (status == SURCO_OK)
        status = read_footer(stream, ctx->meta.file_bytes, ctx->header_size, ctx->meta.indexed, hw, &end);
    if (status == SURCO_OK) {
        ctx->data_end = end.offset;
        status = validate_layout(ctx, &end);
    }
    if (status != SURCO_OK) {
        free(ctx);
        return status;
    }
    *out = ctx;
    return SURCO_OK;
}

static surco_status load_next(surco_decoder *ctx, uint64_t upper)
{
    if (ctx->next == ctx->data_end)
        return SURCO_END;
    block b;
    surco_status status = read_block(ctx->file, ctx->next, ctx->data_end, ctx->meta.schema.channels,
        ctx->ops.hardware_crc, &b);
    if (status == SURCO_OK && b.sequence != ctx->sequence)
        status = SURCO_FORMAT;
    if (status == SURCO_OK && b.first > upper)
        return SURCO_END;
    if (status == SURCO_OK)
        status = read_payload(ctx->file, &b, ctx->meta.schema.channels, &ctx->ops, ctx->payload, &ctx->rows);
    if (status != SURCO_OK) {
        ctx->failed = true;
        return status;
    }
    ctx->next += BLOCK_HEADER + b.size;
    ++ctx->sequence;
    ctx->row = 0;
    ctx->count = b.count;
    return SURCO_OK;
}

surco_status surco_decoder_read_through(surco_decoder *ctx, uint64_t upper,
    uint64_t *timestamp, int32_t *values, size_t capacity)
{
    if (!ctx || !timestamp || !values || capacity < ctx->meta.schema.channels || ctx->failed)
        return SURCO_ARGUMENT;
    if (ctx->row == ctx->count) {
        const surco_status s = load_next(ctx, upper);
        if (s != SURCO_OK)
            return s;
    }
    if (ctx->rows.time[ctx->row] > upper)
        return SURCO_END;
    *timestamp = ctx->rows.time[ctx->row];
    for (uint32_t c = 0; c < ctx->meta.schema.channels; ++c)
        values[c] = as_i32(ctx->rows.channels[c][ctx->row]);
    ++ctx->row;
    return SURCO_OK;
}

surco_status surco_decoder_read(surco_decoder *ctx, uint64_t *timestamp, int32_t *values, size_t capacity)
{
    return surco_decoder_read_through(ctx, UINT64_MAX, timestamp, values, capacity);
}

static surco_status seek_block(surco_decoder *ctx, uint64_t timestamp)
{
    ctx->next = ctx->header_size;
    ctx->sequence = 0;
    if (ctx->meta.indexed) {
        uint64_t lo = 0, hi = ctx->meta.blocks;
        uint64_t candidate = ctx->data_end;
        while (lo < hi) {
            const uint64_t mid = lo + (hi - lo) / 2;
            uint8_t entry[INDEX_ENTRY];
            surco_status status = read_at(ctx->file, ctx->data_end + mid * INDEX_ENTRY, entry, sizeof entry);
            if (status != SURCO_OK)
                return status;
            if (get64(entry + 16) < timestamp) {
                lo = mid + 1;
            } else {
                candidate = get64(entry);
                hi = mid;
            }
        }
        ctx->next = candidate;
        ctx->sequence = lo;
    } else {
        while (ctx->next < ctx->data_end) {
            block b;
            surco_status s = read_block(ctx->file, ctx->next, ctx->data_end, ctx->meta.schema.channels,
                ctx->ops.hardware_crc, &b);
            if (s != SURCO_OK)
                return s;
            if (b.last >= timestamp)
                break;
            ctx->next += BLOCK_HEADER + b.size;
            ++ctx->sequence;
        }
    }
    return SURCO_OK;
}

surco_status surco_decoder_seek(surco_decoder *ctx, uint64_t timestamp)
{
    return surco_decoder_seek_range(ctx, timestamp, UINT64_MAX);
}

/* Bound the first load as well as later read_through calls. */
surco_status surco_decoder_seek_range(surco_decoder *ctx, uint64_t lower, uint64_t upper)
{
    if (!ctx || ctx->failed || lower > upper)
        return SURCO_ARGUMENT;
    ctx->row = ctx->count = 0;
    surco_status status = seek_block(ctx, lower);
    if (status != SURCO_OK) {
        ctx->failed = true;
        return status;
    }
    status = load_next(ctx, upper);
    if (status != SURCO_OK)
        return status;
    while (ctx->row < ctx->count && ctx->rows.time[ctx->row] < lower)
        ++ctx->row;
    if (ctx->row == ctx->count || ctx->rows.time[ctx->row] > upper)
        return SURCO_END;
    return SURCO_OK;
}

void surco_decoder_destroy(surco_decoder *ctx)
{
    free(ctx);
}

surco_status surco_get_metadata(const surco_decoder *ctx, surco_metadata *out)
{
    if (!ctx || !out)
        return SURCO_ARGUMENT;
    *out = ctx->meta;
    return SURCO_OK;
}

surco_status surco_verify(FILE *stream, surco_metadata *out, surco_backend backend)
{
    surco_decoder *ctx;
    surco_status s = surco_decoder_create(&ctx, stream, backend);
    if (s != SURCO_OK)
        return s;
    do {
        s = load_next(ctx, UINT64_MAX);
    } while (s == SURCO_OK);
    if (s == SURCO_END) {
        if (out)
            *out = ctx->meta;
        s = SURCO_OK;
    }
    surco_decoder_destroy(ctx);
    return s;
}

static surco_status resync(FILE *file, uint64_t *offset, uint64_t end, uint64_t *budget,
    uint32_t channels, bool hw, uint64_t sequence, uint64_t last)
{
    uint8_t window[4096 + 3];
    uint64_t pos = *offset;
    while (pos < end && end - pos >= BLOCK_HEADER && *budget) {
        size_t n = sizeof window;
        const uint64_t available = end - pos;
        if (available < n)
            n = (size_t)available;
        if (*budget + 3 < n)
            n = (size_t)*budget + 3;
        surco_status s = read_at(file, pos, window, n);
        if (s != SURCO_OK)
            return s;
        for (size_t i = 0; i + 4 <= n; ++i) {
            --*budget;
            if (memcmp(window + i, "BLK1", 4) == 0) {
                block b;
                s = read_block(file, pos + i, end, channels, hw, &b);
                if (s == SURCO_IO)
                    return s;
                if (s == SURCO_OK && b.sequence >= sequence && b.sequence != UINT64_MAX && b.first >= last) {
                    *offset = pos + i;
                    return SURCO_OK;
                }
            }
        }
        pos += n - 3;
    }
    return SURCO_END;
}

surco_status surco_recover(FILE *input, FILE *output, bool indexed, surco_backend backend,
    surco_recovery *out)
{
    if (!input || !output || input == output || !out)
        return SURCO_ARGUMENT;
    if (!surco_backend_available(backend))
        return SURCO_UNSUPPORTED;

    *out = (surco_recovery){0};
    const surco_ops ops = get_ops(backend);
    surco_schema schema;
    bool source_indexed;
    surco_status s = read_header(input, &schema, &source_indexed, ops.hardware_crc);
    uint64_t end;
    if (s == SURCO_OK)
        s = file_size(input, &end);
    if (s != SURCO_OK)
        return s;
    uint64_t offset = header_size(schema.channels);
    footer f;
    s = read_footer(input, end, offset, source_indexed, ops.hardware_crc, &f);
    if (s == SURCO_IO)
        return s;
    const bool has_footer = s == SURCO_OK;
    if (has_footer)
        end = f.offset;

    surco_decoder *scratch = malloc(sizeof *scratch);
    if (!scratch)
        return SURCO_MEMORY;
    surco_encoder *writer;
    s = surco_encoder_create(&writer, output, &schema, backend, indexed, false);
    if (s != SURCO_OK) {
        free(scratch);
        return s;
    }

    uint64_t sequence = 0, last = 0;
    uint64_t budget = UINT64_C(16) * 1024 * 1024;
    while (offset < end) {
        block b;
        s = read_block(input, offset, end, schema.channels, ops.hardware_crc, &b);
        if (s == SURCO_IO)
            break;
        if (s != SURCO_OK || b.sequence < sequence || b.sequence == UINT64_MAX || b.first < last) {
            ++offset;
            s = resync(input, &offset, end, &budget, schema.channels, ops.hardware_crc, sequence, last);
            if (s != SURCO_OK)
                break;
            continue;
        }
        offset += BLOCK_HEADER + b.size;
        sequence = b.sequence + 1;
        s = read_payload(input, &b, schema.channels, &ops, scratch->payload, &scratch->rows);
        if (s == SURCO_IO)
            break;
        if (s != SURCO_OK) {
            ++out->rejected_blocks;
            out->lost_at_least += b.count;
            continue;
        }
        for (uint32_t i = 0; i < b.count; ++i) {
            int32_t values[SURCO_MAX_CHANNELS];
            for (uint32_t c = 0; c < schema.channels; ++c)
                values[c] = as_i32(scratch->rows.channels[c][i]);
            s = surco_encoder_push(writer, scratch->rows.time[i], values, schema.channels);
            if (s != SURCO_OK)
                goto done;
        }
        last = b.last;
        out->recovered += b.count;
        ++out->accepted_blocks;
    }
    if (s != SURCO_IO)
        s = surco_encoder_finish(writer);
    if (has_footer && f.samples >= out->recovered && f.samples - out->recovered >= out->lost_at_least) {
        out->loss_count_known = true;
        out->lost_at_least = f.samples - out->recovered;
    }
done:
    surco_encoder_destroy(writer);
    free(scratch);
    return s;
}
