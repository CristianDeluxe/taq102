// Tap tempo from touch-down timestamps. Tempo estimation, not beat-phase
// sync: the first tap seeds, each later tap commits the median of the last
// four intervals; an interval under 200 ms is a bounce and is ignored; two
// seconds without a tap starts over. Pure; the caller's clock.
#ifndef DESK_TAP_H
#define DESK_TAP_H

#include <stdint.h>

#define TAP_HISTORY 4
#define TAP_BOUNCE_MS 200
#define TAP_RESET_MS 2000

struct desk_tap {
    int64_t last_ms;            // 0 when nothing is seeded
    int interval[TAP_HISTORY];
    int intervals;              // how many of interval[] are filled, ring
    int next;
};

void desk_tap_init(struct desk_tap *t);
void desk_tap_reset(struct desk_tap *t);
// A tap at `now_ms`. Returns the interval to commit in ms, or 0 when this
// tap only seeds (the first, or the first after a pause) or was a bounce.
int desk_tap_down(struct desk_tap *t, int64_t now_ms);

#endif
