#ifndef SLEEP_STATE_H
#define SLEEP_STATE_H

#include <stdint.h>

struct sleep_timer {
    int64_t deadline_ms;
    int minutes;
};

void sleep_timer_init(struct sleep_timer *timer, int minutes, int64_t now_ms);
void sleep_timer_touch(struct sleep_timer *timer, int64_t now_ms);
void sleep_timer_set_minutes(struct sleep_timer *timer, int minutes,
                             int64_t now_ms);
int sleep_timer_due(const struct sleep_timer *timer, int64_t now_ms,
                    int plugged, int plugged_valid);

struct pickup {
    int have_base;
    int bx;
    int by;
    int bz;
    int votes;
    int64_t slept_at_ms;
    int64_t last_sample_ms;
};

void pickup_start(struct pickup *pickup, int64_t now_ms);
/* Feed each snapshot once, only when accel_snapshot.sequence advances. */
int pickup_feed(struct pickup *pickup, int x, int y, int z, int64_t now_ms);

#endif
