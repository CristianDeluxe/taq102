#ifndef LOOP_TRACE_H
#define LOOP_TRACE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
/* Each translation unit/thread owns its phase list. No I/O below 200 ms. */
static inline int64_t loop_trace(const char *phase, int64_t started) {
    static __thread const char *names[32];
    static __thread int64_t durations[32];
    static __thread int count;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (!phase) { count = 0; return now; }
    int64_t elapsed = now - started;
    if (!strcmp(phase, "iteration")) {
        if (elapsed > 200) {
            fprintf(stderr, "stall: at=%lld phase=iteration duration=%lld ms",
                    (long long)now, (long long)elapsed);
            for (int i = 0; i < count; ++i)
                fprintf(stderr, " %s=%lld", names[i], (long long)durations[i]);
            fputc('\n', stderr);
        }
        count = 0;
    } else {
        if (count < 32) { names[count] = phase; durations[count++] = elapsed; }
        if (elapsed > 200)
            fprintf(stderr, "stall: at=%lld phase=%s duration=%lld ms\n",
                    (long long)now, phase, (long long)elapsed);
    }
    return now;
}
#endif
