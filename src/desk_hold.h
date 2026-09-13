// Pure hold-to-fire model. The caller supplies nonnegative monotonic milliseconds
// and sends each returned action once with qlc_encode_flash. No action is {-1, 0}.
#ifndef DESK_HOLD_H
#define DESK_HOLD_H

#include <stdint.h>

enum hold_kind { HOLD_HIT, HOLD_FOG };
struct hold_action { int widget_id; int on; };
struct desk_hold_control {
    int widget_id;
    enum hold_kind kind;
    int cap_ms;
    int cooldown_ms;
    int down;
    int slot;
    int64_t down_since;
    int capped;
    // -1 means a fog release was returned without a timestamp by desk_hold_owed;
    // the next timestamped mutating operation starts its cooldown conservatively.
    int64_t cooldown_until;
    int owed_release; // An accepted press still needs its one off action.
};
struct desk_hold { struct desk_hold_control control[16]; int count; int link_ready; };

void desk_hold_init(struct desk_hold *h);
// Returns the index, or -1 for invalid settings, duplicate widgets or no room.
int desk_hold_add(struct desk_hold *h, int widget_id, enum hold_kind kind,
                  int cap_ms, int cooldown_ms);
struct hold_action desk_hold_press(struct desk_hold *h, int i, int slot, int64_t now);
struct hold_action desk_hold_release(struct desk_hold *h, int i, int slot, int64_t now);
// Cancels every capture even with no output space or no link. Unreturned releases
// remain pending and block new presses. Batches return actions in control order.
int desk_hold_release_all(struct desk_hold *h, int64_t now, struct hold_action *out, int cap);
// Caps at elapsed >= cap_ms, keeping the finger captured until lift/cancel.
int desk_hold_tick(struct desk_hold *h, int64_t now, struct hold_action *out, int cap);
// A lost link caps held fingers; reconnect never restarts their on actions.
void desk_hold_set_link(struct desk_hold *h, int ready);
// Drains pending offs only when linked; active, uncapped holds are left alone.
// NULL output or nonpositive capacity writes nothing and preserves pending offs.
int desk_hold_owed(struct desk_hold *h, struct hold_action *out, int cap);
int desk_hold_progress(const struct desk_hold *h, int i, int64_t now);
// A release the caller could not send: owed again, drained on the next
// desk_hold_owed once the link is back. Returns 0, or -1 for no such widget.
int desk_hold_unsent(struct desk_hold *h, int widget_id);
// Whether a release is still owed for control i (the output may be on).
int desk_hold_unresolved(const struct desk_hold *h, int i);

#endif
