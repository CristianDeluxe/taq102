#include "sleep_state.h"

#include <limits.h>

enum {
    MILLISECONDS_PER_MINUTE = 60000,
    PICKUP_BASELINE_DELAY_MS = 2000,
    PICKUP_THRESHOLD_MG = 150,
    PICKUP_REQUIRED_VOTES = 3,
};

static int64_t add_duration_saturated(int64_t now_ms, int64_t duration_ms) {
    if (now_ms > INT64_MAX - duration_ms) return INT64_MAX;
    return now_ms + duration_ms;
}

static void reset_deadline(struct sleep_timer *timer, int64_t now_ms) {
    if (timer->minutes <= 0) {
        timer->deadline_ms = INT64_MAX;
        return;
    }

    int64_t duration_ms = (int64_t)timer->minutes * MILLISECONDS_PER_MINUTE;
    timer->deadline_ms = add_duration_saturated(now_ms, duration_ms);
}

void sleep_timer_init(struct sleep_timer *timer, int minutes, int64_t now_ms) {
    timer->minutes = minutes;
    reset_deadline(timer, now_ms);
}

void sleep_timer_touch(struct sleep_timer *timer, int64_t now_ms) {
    reset_deadline(timer, now_ms);
}

void sleep_timer_set_minutes(struct sleep_timer *timer, int minutes,
                             int64_t now_ms) {
    timer->minutes = minutes;
    reset_deadline(timer, now_ms);
}

int sleep_timer_due(const struct sleep_timer *timer, int64_t now_ms,
                    int plugged, int plugged_valid) {
    return timer->minutes > 0 && plugged_valid && !plugged &&
           now_ms >= timer->deadline_ms;
}

void pickup_start(struct pickup *pickup, int64_t now_ms) {
    pickup->have_base = 0;
    pickup->bx = 0;
    pickup->by = 0;
    pickup->bz = 0;
    pickup->votes = 0;
    pickup->slept_at_ms = now_ms;
    pickup->last_sample_ms = INT64_MIN;
}

static int baseline_due(const struct pickup *pickup, int64_t now_ms) {
    int64_t due_ms = add_duration_saturated(pickup->slept_at_ms,
                                            PICKUP_BASELINE_DELAY_MS);
    return now_ms >= due_ms;
}

static int moved_beyond_threshold(int sample, int baseline) {
    int64_t delta = (int64_t)sample - baseline;
    return delta > PICKUP_THRESHOLD_MG || delta < -PICKUP_THRESHOLD_MG;
}

int pickup_feed(struct pickup *pickup, int x, int y, int z, int64_t now_ms) {
    if (!pickup->have_base) {
        if (!baseline_due(pickup, now_ms)) return 0;
        pickup->bx = x;
        pickup->by = y;
        pickup->bz = z;
        pickup->have_base = 1;
        pickup->last_sample_ms = now_ms;
        return 0;
    }

    if (now_ms <= pickup->last_sample_ms) return 0;
    pickup->last_sample_ms = now_ms;

    if (moved_beyond_threshold(x, pickup->bx) ||
        moved_beyond_threshold(y, pickup->by) ||
        moved_beyond_threshold(z, pickup->bz)) {
        pickup->votes++;
    } else {
        pickup->votes = 0;
    }
    return pickup->votes >= PICKUP_REQUIRED_VOTES;
}
