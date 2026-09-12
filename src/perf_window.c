#include "perf_window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void perf_window_init(struct perf_window *w) { memset(w, 0, sizeof *w); }

void perf_window_add(struct perf_window *w, int value) {
    w->sample[w->next] = value;
    w->next = (w->next + 1) % PERF_WINDOW_SIZE;
    if (w->count < 1000000000)
        w->count++;
}

static int compare(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int perf_window_percentile(const struct perf_window *w, int p) {
    int n = w->count < PERF_WINDOW_SIZE ? w->count : PERF_WINDOW_SIZE;
    if (n == 0)
        return -1;
    int sorted[PERF_WINDOW_SIZE];
    memcpy(sorted, w->sample, (size_t)n * sizeof sorted[0]);
    qsort(sorted, (size_t)n, sizeof sorted[0], compare);
    int at = (n - 1) * p / 100;
    return sorted[at];
}

long perf_rss_kb(void) {
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f)
        return -1;
    long size, resident;
    int ok = fscanf(f, "%ld %ld", &size, &resident) == 2;
    fclose(f);
    if (!ok)
        return -1;
    long page = sysconf(_SC_PAGESIZE);
    return resident * (page > 0 ? page : 4096) / 1024;
}

long perf_memavailable_kb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return -1;
    char line[128];
    long value = -1;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemAvailable: %ld kB", &value) == 1)
            break;
    }
    fclose(f);
    return value;
}
