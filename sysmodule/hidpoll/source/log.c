#include "log.h"
#include <stdio.h>
#include <stdarg.h>

void hp_log(const char* fmt, ...) {
    FILE* f = fopen(HP_LOG_PATH, "a");
    if (!f) return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fputc('\n', f);
    fclose(f);
}
