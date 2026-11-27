#include <inttypes.h>
#include <stdio.h>
#include <surco/surco.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fputs("usage: read ARCHIVE\n", stderr);
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return 1;
    }
    surco_decoder *reader;
    surco_status status = surco_decoder_create(&reader, file, SURCO_AUTO);
    if (status == SURCO_OK) {
        uint64_t timestamp;
        int32_t values[SURCO_MAX_CHANNELS];
        while ((status = surco_decoder_read(reader, &timestamp, values, SURCO_MAX_CHANNELS)) == SURCO_OK)
            printf("%" PRIu64 ",%" PRId32 "\n", timestamp, values[0]);
        surco_decoder_destroy(reader);
    }
    fclose(file);
    if (status != SURCO_END) {
        fprintf(stderr, "%s\n", surco_strerror(status));
        return 1;
    }
    return 0;
}
