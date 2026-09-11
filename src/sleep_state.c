#include "sleep_state.h"

#include <limits.h>

enum {
    MILLISECONDS_PER_MINUTE = 60000,
    PICKUP_BASELINE_DELAY_MS = 2000,
    PICKUP_THRESHOLD_MG = 150,
    PICKUP_REQUIRED_VOTES = 3,
    /* Tighter than the wake threshold: the tablet counts as still only when
     * consecutive samples agree this closely.
     */
    PICKUP_SETTLE_THRESHOLD_MG = 60,
    PICKUP_SETTLE_REQUIRED_VOTES = 3,
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

/*
 * The power source does not enter into it. This used to sleep only on
 * battery, which meant an appliance left plugged in never slept at all and
 * the feature looked missing; a tablet that dims itself after a few idle
 * minutes does so on the charger too.
 */
int sleep_timer_due(const struct sleep_timer *timer, int64_t now_ms) {
    return timer->minutes > 0 && now_ms >= timer->deadline_ms;
}

void pickup_start(struct pickup *pickup, int64_t now_ms) {
    pickup->have_base = 0;
    pickup->bx = 0;
    pickup->by = 0;
    pickup->bz = 0;
    pickup->votes = 0;
    pickup->have_settle = 0;
    pickup->sx = 0;
    pickup->sy = 0;
    pickup->sz = 0;
    pickup->settle_votes = 0;
    pickup->slept_at_ms = now_ms;
    pickup->last_sample_ms = INT64_MIN;
}

static int baseline_due(const struct pickup *pickup, int64_t now_ms) {
    int64_t due_ms = add_duration_saturated(pickup->slept_at_ms,
                                            PICKUP_BASELINE_DELAY_MS);
    return now_ms >= due_ms;
}

static int differs_by(int sample, int reference, int limit_mg) {
    int64_t delta = (int64_t)sample - reference;
    return delta > limit_mg || delta < -limit_mg;
}

static int moved_beyond_threshold(int sample, int baseline) {
    return differs_by(sample, baseline, PICKUP_THRESHOLD_MG);
}

/*
 * Arm on stillness rather than on a stopwatch. The baseline used to be taken a
 * flat two seconds after the screen went dark, which is while the hand that
 * pressed the power key is still on the tablet: the baseline was captured
 * mid-movement and the very next sample read as a pick-up, so the tablet woke
 * itself seconds after being told to sleep. Waiting for consecutive samples to
 * agree costs nothing when the tablet is put down and everything when it is
 * not. Samples are compared with their predecessor, so a slow drift never
 * accumulates into a false "still".
 */
static int settled(struct pickup *pickup, int x, int y, int z) {
    if (!pickup->have_settle) {
        pickup->have_settle = 1;
        pickup->settle_votes = 1;
    } else if (differs_by(x, pickup->sx, PICKUP_SETTLE_THRESHOLD_MG) ||
               differs_by(y, pickup->sy, PICKUP_SETTLE_THRESHOLD_MG) ||
               differs_by(z, pickup->sz, PICKUP_SETTLE_THRESHOLD_MG)) {
        pickup->settle_votes = 1;
    } else {
        pickup->settle_votes++;
    }

    pickup->sx = x;
    pickup->sy = y;
    pickup->sz = z;
    return pickup->settle_votes >= PICKUP_SETTLE_REQUIRED_VOTES;
}

int pickup_feed(struct pickup *pickup, int x, int y, int z, int64_t now_ms) {
    if (!pickup->have_base) {
        if (!baseline_due(pickup, now_ms)) return 0;
        if (now_ms <= pickup->last_sample_ms) return 0;
        pickup->last_sample_ms = now_ms;
        if (!settled(pickup, x, y, z)) return 0;
        pickup->bx = x;
        pickup->by = y;
        pickup->bz = z;
        pickup->have_base = 1;
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
