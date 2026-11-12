// check.cc: see check.h.
#include <stdarg.h>
#include "check.h"

int failures;
int checks;

void check(bool condition, const char *what, ...)
{
    char line[300];
    va_list ap;
    va_start(ap, what);
    vsnprintf(line, sizeof(line), what, ap);
    va_end(ap);
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL  %s\n", line);
    } else {
        printf("  ok    %s\n", line);
    }
}
