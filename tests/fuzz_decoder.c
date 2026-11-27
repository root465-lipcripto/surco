#include "surco/surco.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0 || size > 512 * 1024)
        return 0;
    FILE *file = fmemopen((void *)data, size, "rb");
    if (file) {
        surco_verify(file, NULL, SURCO_AUTO);
        fclose(file);
    }
    return 0;
}
