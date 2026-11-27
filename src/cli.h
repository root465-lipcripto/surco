#ifndef SURCO_CLI_H
#define SURCO_CLI_H
#include "surco/surco.h"

enum { CSV_LINE_MAX = 2048 };
int read_line(FILE *file, char *line, size_t capacity);
bool parse_u64(const char *text, uint64_t *value);
bool parse_i32(const char *text, int32_t *value);
bool parse_schema(char *line, surco_schema *schema);
bool parse_row(char *line, uint32_t channels, uint64_t *timestamp, int32_t *values);
bool write_schema(FILE *file, const surco_schema *schema);
bool write_row(FILE *file, uint64_t timestamp, const int32_t *values, uint32_t channels);
const char *unit_name(surco_unit unit);

surco_status capture_serial(const char *device, unsigned baud, unsigned timeout_ms,
    uint64_t limit, uint32_t channels, surco_encoder *writer);
int benchmark_transforms(surco_backend backend, unsigned repetitions);
#endif
