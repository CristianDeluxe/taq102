#include "desk_lock.h"

#include <string.h>

void desk_lock_init(struct desk_lock *l) {
    memset(l, 0, sizeof *l);
    l->state = DESK_UNLOCKED;
}

int desk_lock_target_down(struct desk_lock *l, int64_t now) {
    if (l->state == DESK_BLANKED)
        return 0;
    l->hold_started_ms = now > 0 ? now : 1;
    return 0;
}

int desk_lock_target_up(struct desk_lock *l, int64_t now) {
    if (l->state == DESK_BLANKED || l->hold_started_ms == 0)
        return 0;
    int64_t held = now - l->hold_started_ms;
    l->hold_started_ms = 0;
    if (l->state == DESK_UNLOCKED) {
        // A tap locks. A long press locks too: there is no reason to make
        // locking hard.
        l->state = DESK_LOCKED;
        return 1;
    }
    if (held >= DESK_UNLOCK_HOLD_MS) {
        l->state = DESK_UNLOCKED;
        // The finger that unlocked is still down somewhere; nothing counts
        // until the glass is clear.
        l->need_all_up = l->fingers_down > 0;
        return 1;
    }
    return 0;
}

void desk_lock_contact(struct desk_lock *l, int down) {
    if (down)
        l->fingers_down++;
    else if (l->fingers_down > 0)
        l->fingers_down--;
    if (l->fingers_down == 0)
        l->need_all_up = 0;
}

int desk_lock_power_key(struct desk_lock *l, int64_t now) {
    (void)now;
    if (l->state == DESK_BLANKED) {
        l->state = DESK_LOCKED;
        l->need_all_up = l->fingers_down > 0;
    } else {
        l->state = DESK_BLANKED;
    }
    l->hold_started_ms = 0;
    return 1;
}

int desk_lock_allows(const struct desk_lock *l) {
    return l->state == DESK_UNLOCKED && !l->need_all_up;
}
