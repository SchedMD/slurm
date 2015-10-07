#include "test_common.h"
#include <stdarg.h>

int pmix_test_verbose = 0;

#define OUTPUT_MAX 1024
char *pmix_test_output_prepare(const char *fmt, ...)
{
    static char output[OUTPUT_MAX];
    va_list args;
    va_start(args, fmt);
    memset(output, 0, sizeof(output));
    vsnprintf(output, OUTPUT_MAX - 1, fmt, args);
    return output;
}
