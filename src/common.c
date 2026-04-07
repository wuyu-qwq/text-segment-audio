#include "common.h"

#include <stdarg.h>
#include <stdio.h>

void set_error(char *err, size_t err_size, const char *fmt, ...)
{
    va_list args;

    if (err == NULL || err_size == 0U) {
        return;
    }

    va_start(args, fmt);
    (void)vsnprintf(err, err_size, fmt, args);
    va_end(args);

    err[err_size - 1U] = '\0';
}
