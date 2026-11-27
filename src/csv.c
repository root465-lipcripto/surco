#include "cli.h"

#include <inttypes.h>
#include <string.h>

int read_line(FILE *file, char *line, size_t capacity)
{
    size_t size = 0;
    int ch;
    while ((ch = fgetc(file)) != EOF && ch != '\n') {
        if (!ch || size == capacity - 1)
            return -1;
        line[size++] = (char)ch;
    }
    if (ferror(file))
        return -2;
    if (size == 0 && ch == EOF)
        return 0;
    if (ch == '\n' && size && line[size - 1] == '\r')
        --size;
    line[size] = '\0';
    return 1;
}

bool parse_u64(const char *text, uint64_t *value)
{
    uint64_t number = 0;
    if (!*text)
        return false;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9')
            return false;
        const unsigned digit = (unsigned)(*p - '0');
        if (number > (UINT64_MAX - digit) / 10)
            return false;
        number = number * 10 + digit;
    }
    *value = number;
    return true;
}

bool parse_i32(const char *text, int32_t *value)
{
    const bool negative = text[0] == '-';
    uint64_t magnitude;
    if (!parse_u64(text + negative, &magnitude) || magnitude > (uint64_t)INT32_MAX + negative)
        return false;
    *value = (int32_t)(negative ? -(int64_t)magnitude : (int64_t)magnitude);
    return true;
}

static char *next_field(char **rest)
{
    char *field = *rest;
    if (field) {
        *rest = strchr(field, ',');
        if (*rest)
            *(*rest)++ = '\0';
    }
    return field;
}

const char *unit_name(surco_unit unit)
{
    static const char *const units[] = {"us", "ns", "ms", "s"};
    return units[unit];
}

bool parse_schema(char *line, surco_schema *schema)
{
    *schema = (surco_schema){0};
    const char *timestamp = next_field(&line);
    if (strncmp(timestamp, "timestamp_", 10) != 0)
        return false;
    const char *suffix = timestamp + 10;
    bool found = false;
    for (unsigned i = 0; i <= SURCO_S; ++i) {
        if (strcmp(suffix, unit_name((surco_unit)i)) == 0) {
            schema->unit = (surco_unit)i;
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    const char *name;
    while ((name = next_field(&line)) != NULL) {
        const size_t length = strlen(name);
        if (length >= SURCO_NAME_SIZE || schema->channels == SURCO_MAX_CHANNELS)
            return false;
        memcpy(schema->names[schema->channels++], name, length + 1);
    }
    return surco_validate_schema(schema) == SURCO_OK;
}

bool parse_row(char *line, uint32_t channels, uint64_t *timestamp, int32_t *values)
{
    if (!parse_u64(next_field(&line), timestamp))
        return false;
    for (uint32_t i = 0; i < channels; ++i) {
        const char *field = next_field(&line);
        if (!field || !parse_i32(field, &values[i]))
            return false;
    }
    return line == NULL;
}

bool write_schema(FILE *file, const surco_schema *schema)
{
    if (fprintf(file, "timestamp_%s", unit_name(schema->unit)) < 0)
        return false;
    for (uint32_t i = 0; i < schema->channels; ++i)
        if (fprintf(file, ",%s", schema->names[i]) < 0)
            return false;
    return fputc('\n', file) != EOF;
}

bool write_row(FILE *file, uint64_t timestamp, const int32_t *values, uint32_t channels)
{
    if (fprintf(file, "%" PRIu64, timestamp) < 0)
        return false;
    for (uint32_t i = 0; i < channels; ++i)
        if (fprintf(file, ",%" PRId32, values[i]) < 0)
            return false;
    return fputc('\n', file) != EOF;
}
