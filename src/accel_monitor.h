#ifndef ACCEL_MONITOR_H
#define ACCEL_MONITOR_H

#include <stdint.h>

struct accel_monitor;

struct accel_snapshot {
    unsigned sequence;
    int x_mg;
    int y_mg;
    int z_mg;
    int64_t at_ms;
    long max_read_us;
};

struct accel_monitor *accel_monitor_start(void);
int accel_monitor_snapshot(struct accel_monitor *monitor,
                           struct accel_snapshot *snapshot);

#endif
