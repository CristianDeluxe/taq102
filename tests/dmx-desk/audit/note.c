#include "api.h"
#include <stdarg.h>
void audit_note(struct audit_context *a, int warning, const char *rule, const char *fmt, ...) {
    va_list args;
    if (warning) a->warnings++; else a->failures++;
    fprintf(a->report, "%s [%s] %s: ", warning ? "WARN" : "FAIL", rule, a->view);
    va_start(args, fmt);
    vfprintf(a->report, fmt, args);
    va_end(args);
    fputc('\n', a->report);
}
