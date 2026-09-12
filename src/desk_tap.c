#include "desk_tap.h"

#include <string.h>

void desk_tap_init(struct desk_tap *t) {
    memset(t, 0, sizeof *t);
}

void desk_tap_reset(struct desk_tap *t) {
    desk_tap_init(t);
}

static int median(const struct desk_tap *t) {
    int sorted[TAP_HISTORY];
    int n = t->intervals;
    memcpy(sorted, t->interval, sizeof sorted);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) {
            int tmp = sorted[j];
            sorted[j] = sorted[j - 1];
            sorted[j - 1] = tmp;
        }
    // An even count takes the mean of the middle pair, as the engine does not
    // (it takes the upper); the difference is a millisecond on a bounce.
    return n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
}

int desk_tap_down(struct desk_tap *t, int64_t now_ms) {
    if (t->last_ms == 0 || now_ms - t->last_ms > TAP_RESET_MS) {
        desk_tap_init(t);
        t->last_ms = now_ms;
        return 0;
    }
    int64_t gap = now_ms - t->last_ms;
    if (gap < TAP_BOUNCE_MS)
        return 0;
    t->last_ms = now_ms;
    t->interval[t->next] = (int)gap;
    t->next = (t->next + 1) % TAP_HISTORY;
    if (t->intervals < TAP_HISTORY)
        t->intervals++;
    return median(t);
}
