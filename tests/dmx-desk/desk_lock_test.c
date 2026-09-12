// SOURCES: desk_lock.c
// Lock by a tap, unlock by a held second, and never let a finger that was
// already on the glass count as a fresh touch after an unlock or a wake.
#include <assert.h>
#include <stdio.h>

#include "desk_lock.h"

int main(void) {
    struct desk_lock l;
    desk_lock_init(&l);
    assert(l.state == DESK_UNLOCKED && desk_lock_allows(&l));

    // A tap on the target locks.
    desk_lock_contact(&l, 1);
    desk_lock_target_down(&l, 1000);
    assert(desk_lock_target_up(&l, 1100) == 1);
    desk_lock_contact(&l, 0);
    assert(l.state == DESK_LOCKED && !desk_lock_allows(&l));

    // A tap does not unlock; a held second does.
    desk_lock_contact(&l, 1);
    desk_lock_target_down(&l, 2000);
    assert(desk_lock_target_up(&l, 2300) == 0);
    desk_lock_contact(&l, 0);
    assert(l.state == DESK_LOCKED);
    desk_lock_contact(&l, 1);
    desk_lock_target_down(&l, 3000);
    assert(desk_lock_target_up(&l, 4050) == 1);
    assert(l.state == DESK_UNLOCKED);
    // The unlocking finger is still down: nothing passes until it lifts.
    assert(!desk_lock_allows(&l));
    desk_lock_contact(&l, 0);
    assert(desk_lock_allows(&l));

    // A second finger down during the unlock keeps the gate shut until both lift.
    desk_lock_contact(&l, 1);
    desk_lock_target_down(&l, 5000);
    desk_lock_contact(&l, 1);
    assert(desk_lock_target_up(&l, 5000) == 1);   // locks (it was unlocked)
    assert(l.state == DESK_LOCKED);
    desk_lock_target_down(&l, 6000);
    assert(desk_lock_target_up(&l, 7100) == 1);
    assert(l.state == DESK_UNLOCKED && !desk_lock_allows(&l));
    desk_lock_contact(&l, 0);
    assert(!desk_lock_allows(&l));
    desk_lock_contact(&l, 0);
    assert(desk_lock_allows(&l));

    // The power key blanks and locks; the next press wakes to locked, and a
    // finger down at the wake is not delivered.
    assert(desk_lock_power_key(&l, 8000) == 1 && l.state == DESK_BLANKED);
    assert(!desk_lock_allows(&l));
    desk_lock_target_down(&l, 8100);
    assert(desk_lock_target_up(&l, 9500) == 0);    // the target is dark
    desk_lock_contact(&l, 1);
    assert(desk_lock_power_key(&l, 9600) == 1 && l.state == DESK_LOCKED);
    assert(!desk_lock_allows(&l));
    desk_lock_contact(&l, 0);
    desk_lock_target_down(&l, 10000);
    assert(desk_lock_target_up(&l, 11100) == 1 && l.state == DESK_UNLOCKED);
    assert(desk_lock_allows(&l));
    printf("desk_lock ok\n");
    return 0;
}
