#include "accel_monitor.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "accel.h"

struct accel_monitor {
    pthread_mutex_t mutex;
    unsigned sequence;
    int x_mg;
    int y_mg;
    int z_mg;
    int64_t at_ms;
    long max_read_us;
};

static long elapsed_us(const struct timespec *start, const struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000000L +
           (end->tv_nsec - start->tv_nsec) / 1000L;
}

static int64_t monotonic_ms(const struct timespec *at) {
    return (int64_t)at->tv_sec * 1000 + at->tv_nsec / 1000000;
}

static void *read_accelerometer(void *arg) {
    struct accel_monitor *monitor = arg;
    int fd = accel_open();
    if (fd < 0) return NULL;

    const struct timespec interval = { .tv_sec = 0, .tv_nsec = 200000000L };
    for (;;) {
        int x_mg, y_mg, z_mg;
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        int result = accel_read(fd, &x_mg, &y_mg, &z_mg);
        clock_gettime(CLOCK_MONOTONIC, &end);
        long read_us = elapsed_us(&start, &end);

        if (result == 0) {
            pthread_mutex_lock(&monitor->mutex);
            monitor->x_mg = x_mg;
            monitor->y_mg = y_mg;
            monitor->z_mg = z_mg;
            monitor->at_ms = monotonic_ms(&end);
            monitor->sequence++;
            if (read_us > monitor->max_read_us) monitor->max_read_us = read_us;
            pthread_mutex_unlock(&monitor->mutex);
        }
        nanosleep(&interval, NULL);
    }
}

struct accel_monitor *accel_monitor_start(void) {
    struct accel_monitor *monitor = calloc(1, sizeof *monitor);
    if (!monitor) return NULL;
    if (pthread_mutex_init(&monitor->mutex, NULL) != 0) {
        free(monitor);
        return NULL;
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, read_accelerometer, monitor) != 0) {
        pthread_mutex_destroy(&monitor->mutex);
        free(monitor);
        return NULL;
    }
    pthread_detach(thread);
    return monitor;
}

int accel_monitor_snapshot(struct accel_monitor *monitor,
                           struct accel_snapshot *snapshot) {
    if (!monitor || !snapshot) return -1;

    pthread_mutex_lock(&monitor->mutex);
    *snapshot = (struct accel_snapshot) {
        .sequence = monitor->sequence,
        .x_mg = monitor->x_mg,
        .y_mg = monitor->y_mg,
        .z_mg = monitor->z_mg,
        .at_ms = monitor->at_ms,
        .max_read_us = monitor->max_read_us,
    };
    pthread_mutex_unlock(&monitor->mutex);
    return snapshot->sequence == 0 ? -1 : 0;
}
