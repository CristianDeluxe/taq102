// Show mode, the lock and the power key: what a tablet handed around a room
// does with its surface. Pure; the caller turns the display on and off.
//
// Show mode is the default: the desk never blanks by itself, whatever the
// appliance's idle timer thinks, because a desk waiting for the next cue is
// idle by that definition. Locked, the surface accepts nothing while the
// state keeps updating; blanked, the display is off with the link alive.
#ifndef DESK_LOCK_H
#define DESK_LOCK_H

#include <stdint.h>

#define DESK_UNLOCK_HOLD_MS 1000

enum desk_lock_state { DESK_UNLOCKED, DESK_LOCKED, DESK_BLANKED };

struct desk_lock {
    enum desk_lock_state state;
    int64_t hold_started_ms;    // 0 when no finger is on the lock target
    int fingers_down;           // contacts anywhere on the surface
    int need_all_up;            // after an unlock or a wake: nothing counts until every finger lifts
};

void desk_lock_init(struct desk_lock *l);

// The lock target. A tap locks an unlocked desk; on a locked desk a hold of
// DESK_UNLOCK_HOLD_MS unlocks on release, and a shorter press does nothing.
// Each returns 1 when the state changed.
int desk_lock_target_down(struct desk_lock *l, int64_t now);
int desk_lock_target_up(struct desk_lock *l, int64_t now);

// Every contact anywhere, down or up, so that after an unlock or a wake the
// fingers already on the glass are not delivered as fresh touches.
void desk_lock_contact(struct desk_lock *l, int down);

// The power key's short press: blanks a lit desk (locking it), wakes a
// blanked one to LOCKED. Returns 1 when the display must change; the caller
// reads the new state to know which way.
int desk_lock_power_key(struct desk_lock *l, int64_t now);

// Whether a control input may pass right now.
int desk_lock_allows(const struct desk_lock *l);

#endif
