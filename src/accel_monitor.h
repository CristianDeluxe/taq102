#ifndef ACCEL_MONITOR_H
#define ACCEL_MONITOR_H

struct accel_monitor;

struct accel_snapshot {
    unsigned sequence;
    int y_mg;
    long max_read_us;
};

struct accel_monitor *accel_monitor_start(void);
int accel_monitor_snapshot(struct accel_monitor *monitor,
                           struct accel_snapshot *snapshot);

#endif
