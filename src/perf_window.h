// A window of the last thousand measurements and their percentiles, plus the
// two memory figures a perf gate reads. Diagnostics only: nothing here is on
// the desk's path unless asked for.
#ifndef PERF_WINDOW_H
#define PERF_WINDOW_H

#define PERF_WINDOW_SIZE 1000

struct perf_window {
    int sample[PERF_WINDOW_SIZE];
    int count;      // samples taken in total
    int next;       // ring position
};

void perf_window_init(struct perf_window *w);
void perf_window_add(struct perf_window *w, int value);
// The p-th percentile of the window's samples, or -1 when it is empty.
int perf_window_percentile(const struct perf_window *w, int p);
// The process's resident set from /proc/self/statm, or -1.
long perf_rss_kb(void);
// MemAvailable from /proc/meminfo, or -1.
long perf_memavailable_kb(void);

#endif
