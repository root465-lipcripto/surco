#include "cli.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    OUT = 1u << 0, BACKEND = 1u << 1, RAW = 1u << 2, JSON = 1u << 3,
    FROM = 1u << 4, TO = 1u << 5, DEVICE = 1u << 6, CHANNELS = 1u << 7,
    BAUD = 1u << 8, TIMEOUT = 1u << 9, SAMPLES = 1u << 10, SPEED = 1u << 11,
    FAST = 1u << 12, REPS = 1u << 13, UNINDEXED = 1u << 14
};

typedef enum { PACK, UNPACK, INSPECT, VERIFY, QUERY, RECOVER, BENCHMARK, CAPTURE, REPLAY } command;

typedef struct {
    const char *name;
    command id;
    unsigned allowed, required;
    bool input;
} command_spec;

static const command_spec commands[] = {
    {"pack", PACK, OUT | BACKEND | RAW | UNINDEXED, 0, true},
    {"unpack", UNPACK, OUT | BACKEND, 0, true},
    {"inspect", INSPECT, BACKEND | JSON, 0, true},
    {"verify", VERIFY, BACKEND, 0, true},
    {"query", QUERY, OUT | BACKEND | FROM | TO, 0, true},
    {"recover", RECOVER, OUT | BACKEND, OUT, true},
    {"benchmark", BENCHMARK, BACKEND | REPS, 0, false},
    {"capture", CAPTURE, OUT | BACKEND | DEVICE | CHANNELS | BAUD | TIMEOUT | SAMPLES, OUT | DEVICE, false},
    {"replay", REPLAY, OUT | BACKEND | SPEED | FAST, 0, true}
};

typedef struct {
    const char *input, *output, *device;
    uint64_t from, to, samples;
    unsigned baud, timeout, channels, speed, repetitions, flags;
    surco_backend backend;
} options;

static void usage(FILE *out)
{
    fputs("surco " SURCO_VERSION " — Small records. Long histories.\n"
          "  surco --help | --version | doctor\n"
          "  surco pack INPUT [-o OUTPUT] [--raw] [--no-index]\n"
          "  surco unpack INPUT [-o OUTPUT]\n"
          "  surco inspect INPUT [--json]\n"
          "  surco verify INPUT\n"
          "  surco query INPUT [--from TIME] [--to TIME] [-o OUTPUT]\n"
          "  surco recover INPUT --output OUTPUT\n"
          "  surco benchmark [--repetitions N]\n"
          "  surco capture --device PATH -o OUTPUT [--channels N] [--baud N]\n"
          "                [--timeout-ms N] [--samples N]\n"
          "  surco replay INPUT [--speed N | --fast] [-o OUTPUT]\n"
          "All subcommands above accept --backend auto|scalar|sse2.\n"
          "Use '-' for standard streams. Binary input must be seekable.\n", out);
}

static bool set_backend(const char *value, surco_backend *backend)
{
    for (unsigned i = 0; i <= SURCO_SSE2; ++i)
        if (strcmp(value, surco_backend_name((surco_backend)i)) == 0) {
            *backend = (surco_backend)i;
            return true;
        }
    return false;
}

static bool bounded(const char *text, unsigned max, unsigned *value)
{
    uint64_t n;
    if (!parse_u64(text, &n) || !n || n > max)
        return false;
    *value = (unsigned)n;
    return true;
}

static unsigned match_option(const char *arg)
{
    static const struct { const char *name; unsigned flag; } names[] = {
        {"-o", OUT}, {"--output", OUT}, {"--backend", BACKEND}, {"--raw", RAW},
        {"--json", JSON}, {"--from", FROM}, {"--to", TO}, {"--device", DEVICE},
        {"--channels", CHANNELS}, {"--baud", BAUD}, {"--timeout-ms", TIMEOUT},
        {"--samples", SAMPLES}, {"--speed", SPEED}, {"--fast", FAST}, {"--repetitions", REPS},
        {"--no-index", UNINDEXED}
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i)
        if (strcmp(arg, names[i].name) == 0)
            return names[i].flag;
    return 0;
}

static bool assign_option(options *o, unsigned flag, const char *value)
{
    switch (flag) {
    case OUT: o->output = value; return *value != 0;
    case DEVICE: o->device = value; return *value != 0;
    case BACKEND: return set_backend(value, &o->backend);
    case FROM: return parse_u64(value, &o->from);
    case TO: return parse_u64(value, &o->to);
    case CHANNELS: return bounded(value, SURCO_MAX_CHANNELS, &o->channels);
    case BAUD: return bounded(value, 115200, &o->baud);
    case TIMEOUT: return bounded(value, 3600000, &o->timeout);
    case SAMPLES: return parse_u64(value, &o->samples) && o->samples != 0;
    case SPEED: return bounded(value, 1000000, &o->speed);
    case REPS: return bounded(value, 31, &o->repetitions);
    default: return false;
    }
}

static bool parse_options(const command_spec *spec, int argc, char **argv, options *o)
{
    *o = (options){.output = "-", .to = UINT64_MAX, .samples = UINT64_MAX,
        .baud = 115200, .timeout = 3000, .channels = 1, .speed = 1, .repetitions = 7};
    bool positional = false;
    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        if (!positional && strcmp(arg, "--") == 0) {
            positional = true;
            continue;
        }
        if (positional || arg[0] != '-' || strcmp(arg, "-") == 0) {
            if (o->input || !spec->input)
                return false;
            o->input = arg;
            continue;
        }
        const unsigned flag = match_option(arg);
        if (!(flag & spec->allowed) || (flag & o->flags))
            return false;
        o->flags |= flag;
        if (!(flag & (RAW | JSON | FAST | UNINDEXED)) &&
            (++i == argc || !assign_option(o, flag, argv[i])))
            return false;
    }
    return (!spec->input || o->input) && (o->flags & spec->required) == spec->required &&
        o->from <= o->to && (o->flags & (FAST | SPEED)) != (FAST | SPEED) &&
        (spec->id != RECOVER || strcmp(o->output, "-") != 0);
}

typedef struct {
    FILE *file;
    const char *destination;
    char *staged;
} output;

static bool open_output(output *out, const char *path, bool update)
{
    *out = (output){.destination = path};
    if (strcmp(path, "-") == 0) {
        out->file = stdout;
        return true;
    }
    const size_t n = strlen(path) + sizeof ".surco-tmp-XXXXXX";
    out->staged = malloc(n);
    if (!out->staged)
        return false;
    snprintf(out->staged, n, "%s.surco-tmp-XXXXXX", path);
    const int fd = mkstemp(out->staged);
    if (fd >= 0) {
        out->file = fdopen(fd, update ? "w+b" : "wb");
        if (out->file)
            return true;
        close(fd);
        unlink(out->staged);
    }
    perror(path);
    free(out->staged);
    return false;
}

static bool finish_output(output *out, bool success)
{
    if (!out->staged)
        return !success || fflush(out->file) == 0;
    if (fclose(out->file) != 0)
        success = false;
    if (success && link(out->staged, out->destination) != 0) {
        perror(out->destination);
        success = false;
    }
    if (unlink(out->staged) != 0)
        perror(out->staged);
    free(out->staged);
    return success;
}

static surco_status import_csv(FILE *input, FILE *out, const options *o, bool indexed)
{
    char line[CSV_LINE_MAX];
    surco_schema schema;
    const int header = read_line(input, line, sizeof line);
    if (header != 1 || !parse_schema(line, &schema)) {
        fputs("pack: line 1: invalid or missing CSV schema\n", stderr);
        return header == -2 ? SURCO_IO : SURCO_FORMAT;
    }
    surco_encoder *writer;
    surco_status status = surco_encoder_create(&writer, out, &schema, o->backend,
        indexed, (o->flags & RAW) != 0);
    if (status != SURCO_OK)
        return status;
    uint64_t number = 1;
    int result;
    while ((result = read_line(input, line, sizeof line)) != 0) {
        uint64_t timestamp;
        int32_t values[SURCO_MAX_CHANNELS];
        ++number;
        if (result != 1 || !parse_row(line, schema.channels, &timestamp, values)) {
            status = result == -2 ? SURCO_IO : SURCO_FORMAT;
        } else {
            status = surco_encoder_push(writer, timestamp, values, schema.channels);
        }
        if (status != SURCO_OK) {
            fprintf(stderr, "pack: line %" PRIu64 ": %s\n", number, surco_strerror(status));
            break;
        }
    }
    if (status == SURCO_OK)
        status = surco_encoder_finish(writer);
    surco_encoder_destroy(writer);
    return status;
}

static void inspect(const surco_metadata *m, const options *o)
{
    if (o->flags & JSON) {
        printf("{\"version\":1,\"indexed\":%s,\"timestamp_unit\":\"%s\","
            "\"samples\":%" PRIu64 ",\"blocks\":%" PRIu64 ",\"compressed_blocks\":%" PRIu64
            ",\"raw_blocks\":%" PRIu64 ",\"file_bytes\":%" PRIu64 ",\"raw_bytes\":%" PRIu64
            ",\"first\":%" PRIu64 ",\"last\":%" PRIu64 ",\"backend\":\"%s\",\"channels\":[",
            m->indexed ? "true" : "false", unit_name(m->schema.unit), m->samples, m->blocks,
            m->compressed_blocks, m->raw_blocks, m->file_bytes, m->raw_bytes, m->first, m->last,
            surco_backend_name(surco_resolve_backend(o->backend)));
        for (uint32_t i = 0; i < m->schema.channels; ++i)
            printf("%s\"%s\"", i == 0 ? "" : ",", m->schema.names[i]);
        puts("]}");
    } else {
        printf("format v1, %" PRIu64 " samples, %" PRIu64 " blocks (%" PRIu64 " compressed), "
            "%" PRIu64 " file bytes, %" PRIu64 " raw bytes\n", m->samples, m->blocks,
            m->compressed_blocks, m->file_bytes, m->raw_bytes);
        printf("timestamps %" PRIu64 "..%" PRIu64 " %s, index %s\n", m->first,
            m->last, unit_name(m->schema.unit), m->indexed ? "present" : "absent");
        printf("channels");
        for (uint32_t i = 0; i < m->schema.channels; ++i)
            printf(" %s", m->schema.names[i]);
        putchar('\n');
    }
}

static surco_status replay_wait(uint64_t gap, surco_unit unit, unsigned speed)
{
    static const uint64_t ns_per_tick[] = {1000, 1, 1000000, 1000000000};
    const uint64_t max_ns = UINT64_C(60000000000);
    const uint64_t scaled_limit = max_ns * speed;
    if (gap > scaled_limit / ns_per_tick[unit]) {
        fputs("replay: scaled gap exceeds 60 seconds. Use --fast or increase --speed.\n", stderr);
        return SURCO_LIMIT;
    }
    const uint64_t ns = gap * ns_per_tick[unit] / speed;
    struct timespec wait = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
    while (nanosleep(&wait, &wait) != 0)
        if (errno != EINTR)
            return SURCO_IO;
    return SURCO_OK;
}

static surco_status export_csv(surco_decoder *reader, FILE *out, const surco_metadata *meta,
    const options *o, command id)
{
    surco_status status = SURCO_OK;
    if (id == QUERY)
        status = surco_decoder_seek_range(reader, o->from, o->to);
    if (status != SURCO_OK && status != SURCO_END)
        return status;
    const bool timed = id == REPLAY && !(o->flags & FAST);
    if (!write_schema(out, &meta->schema) || (timed && fflush(out) != 0))
        return SURCO_IO;
    if (status == SURCO_END)
        return SURCO_OK;

    uint64_t timestamp, previous = 0;
    bool first = true;
    int32_t values[SURCO_MAX_CHANNELS];
    while ((status = surco_decoder_read_through(reader, o->to, &timestamp,
        values, SURCO_MAX_CHANNELS)) == SURCO_OK) {
        if (timed && !first && timestamp != previous) {
            status = replay_wait(timestamp - previous, meta->schema.unit, o->speed);
            if (status != SURCO_OK)
                return status;
        }
        if (!write_row(out, timestamp, values, meta->schema.channels) || (timed && fflush(out) != 0))
            return SURCO_IO;
        first = false;
        previous = timestamp;
    }
    return status == SURCO_END ? SURCO_OK : status;
}

static surco_status pack_or_capture(command id, FILE *in, const options *o)
{
    output out;
    if (!open_output(&out, o->output, true))
        return SURCO_IO;
    const bool indexed = out.staged && !(o->flags & UNINDEXED);
    surco_status status;
    if (id == PACK) {
        status = import_csv(in, out.file, o, indexed);
    } else {
        surco_schema schema = {.unit = SURCO_US, .channels = o->channels};
        for (uint32_t i = 0; i < schema.channels; ++i)
            snprintf(schema.names[i], SURCO_NAME_SIZE, "channel_%" PRIu32, i);
        surco_encoder *writer;
        status = surco_encoder_create(&writer, out.file, &schema, o->backend, indexed, false);
        if (status == SURCO_OK)
            status = capture_serial(o->device, o->baud, o->timeout, o->samples, o->channels, writer);
        if (status == SURCO_OK)
            status = surco_encoder_finish(writer);
        surco_encoder_destroy(writer);
    }
    if (!finish_output(&out, status == SURCO_OK) && status == SURCO_OK)
        return SURCO_IO;
    return status;
}

static surco_status recover(FILE *in, const options *o)
{
    output out;
    if (!open_output(&out, o->output, true))
        return SURCO_IO;
    surco_recovery stats;
    surco_status status = surco_recover(in, out.file, true, o->backend, &stats);
    if (status == SURCO_OK)
        status = surco_verify(out.file, NULL, o->backend);
    if (!finish_output(&out, status == SURCO_OK) && status == SURCO_OK)
        return SURCO_IO;
    if (status == SURCO_OK)
        printf("{\"recovered\":%" PRIu64 ",\"lost_at_least\":%" PRIu64
            ",\"loss_count_known\":%s,\"accepted_blocks\":%" PRIu64
            ",\"rejected_payload_blocks\":%" PRIu64 "}\n", stats.recovered,
            stats.lost_at_least, stats.loss_count_known ? "true" : "false",
            stats.accepted_blocks, stats.rejected_blocks);
    return status;
}

static surco_status run(command id, FILE *in, const options *o)
{
    if (id == PACK || id == CAPTURE)
        return pack_or_capture(id, in, o);
    if (id == RECOVER)
        return recover(in, o);
    if (id == VERIFY) {
        surco_metadata meta;
        const surco_status status = surco_verify(in, &meta, o->backend);
        if (status == SURCO_OK)
            printf("verified %" PRIu64 " samples in %" PRIu64 " blocks\n", meta.samples, meta.blocks);
        return status;
    }

    surco_decoder *reader;
    surco_status status = surco_decoder_create(&reader, in, o->backend);
    if (status != SURCO_OK)
        return status;
    surco_metadata meta;
    surco_get_metadata(reader, &meta);
    if (id == INSPECT) {
        inspect(&meta, o);
    } else {
        output out;
        if (open_output(&out, o->output, false)) {
            status = export_csv(reader, out.file, &meta, o, id);
            if (!finish_output(&out, status == SURCO_OK) && status == SURCO_OK)
                status = SURCO_IO;
        } else {
            status = SURCO_IO;
        }
    }
    surco_decoder_destroy(reader);
    return status;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("surco " SURCO_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "doctor") == 0) {
        printf("surco %s\nscalar: available\nsse2: %s\nsse4.2 crc32c: %s\nauto: %s\n",
            SURCO_VERSION, surco_backend_available(SURCO_SSE2) ? "available" : "unavailable",
            surco_hardware_crc_available() ? "available" : "unavailable",
            surco_backend_name(surco_resolve_backend(SURCO_AUTO)));
        return 0;
    }

    const command_spec *spec = NULL;
    if (argc >= 2)
        for (size_t i = 0; i < sizeof commands / sizeof *commands; ++i)
            if (strcmp(argv[1], commands[i].name) == 0)
                spec = &commands[i];

    options o;
    if (!spec || !parse_options(spec, argc - 2, argv + 2, &o)) {
        usage(stderr);
        return 2;
    }
    if (!surco_backend_available(o.backend)) {
        fputs("surco: requested backend is unavailable\n", stderr);
        return 2;
    }
    if (spec->id == BENCHMARK)
        return benchmark_transforms(o.backend, o.repetitions);

    FILE *in = NULL;
    if (o.input) {
        in = strcmp(o.input, "-") == 0 ? stdin : fopen(o.input, "rb");
        if (!in) {
            perror(o.input);
            return 1;
        }
    }
    surco_status status = run(spec->id, in, &o);
    if (in && in != stdin)
        fclose(in);
    if (status == SURCO_OK && fflush(stdout) != 0)
        status = SURCO_IO;
    if (status != SURCO_OK) {
        fprintf(stderr, "%s: %s\n", spec->name, surco_strerror(status));
        return status == SURCO_ARGUMENT ? 2 : 1;
    }
    return 0;
}
