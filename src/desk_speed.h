// The speed cards: one per dial the map carries. Pure. Base time and factor
// come from the master (the snapshot, then SPEED_STATE pushes) and nothing
// the finger does is painted as the rig's until the master echoes it. One
// change outstanding per dial; a same-value change is never sent, because
// the engine's setters return on equality and push nothing. Tap commits on
// the touch-down edge; every other target on release inside itself.
#ifndef DESK_SPEED_H
#define DESK_SPEED_H

#include <stdint.h>

#include "desk_tap.h"
#include "showmap.h"
#include "vcjson.h"

#define SPEED_PENDING_MS 1500
#define SPEED_NOTE_MS 3000
#define SPEED_MIN_MS 200
#define SPEED_MAX_MS 2000

enum speed_action_kind { SPEED_ACT_NONE, SPEED_ACT_TIME, SPEED_ACT_FACTOR, SPEED_ACT_TIME_BOTH };

struct speed_action {
    enum speed_action_kind kind;
    int widget_id;
    int ms;             // SPEED_ACT_TIME, SPEED_ACT_TIME_BOTH (first dial)
    int factor;         // SPEED_ACT_FACTOR
    int widget_id2;     // SPEED_ACT_TIME_BOTH: -1 when the second dial needs no frame
    int ms2;
};

// The targets of a card, in the order the painter and the hit test share.
enum speed_target {
    SPEED_T_NONE, SPEED_T_TAP, SPEED_T_BPM_DOWN, SPEED_T_BPM_UP, SPEED_T_FACTOR_ONE,
    SPEED_T_HALF, SPEED_T_DOUBLE, SPEED_T_BOTH,
};

struct desk_dial {
    char caption[MAP_CAPTION_MAX];
    int widget_id;
    int members;
    int base_ms, factor;        // the master's word
    int known;                  // both are the master's, not a guess
    int enabled;                // the console has this dial
    char reason[48];            // why not, when not
    int max_ms;                 // the ceiling: min(2000, the dial's timeMax)
    int pending;                // a change sent, no echo yet
    int pending_ms, pending_factor;
    int64_t pending_since;
    char note[32];              // "State updated", "No answer"
    int64_t note_until;
};

struct desk_speed {
    struct desk_dial dial[MAP_MAX_DIALS];
    int dials;
    int link_ready;
    struct desk_tap tap[MAP_MAX_DIALS + 1];   // the last one is Tap both's
    enum speed_target capture;
    int capture_dial;
    int refresh_wanted;         // a timeout asked for a fresh snapshot; the caller clears it
    int dirty;
};

void desk_speed_init(struct desk_speed *s, const struct show_map *map);
// The console's word on each dial: whether it exists, its time, factor and range.
void desk_speed_validate(struct desk_speed *s, const struct vc_doc *console);

struct speed_action desk_speed_touch_down(struct desk_speed *s, int x, int y, int64_t now_ms);
struct speed_action desk_speed_touch_up(struct desk_speed *s, int x, int y, int64_t now_ms);
void desk_speed_touch_cancel(struct desk_speed *s);

// A SPEED_STATE push, or the snapshot's values through validate.
void desk_speed_apply(struct desk_speed *s, int widget_id, int ms, int factor, int64_t now_ms);
void desk_speed_set_link(struct desk_speed *s, int ready, int64_t now_ms);
void desk_speed_tick(struct desk_speed *s, int64_t now_ms);
// Resets the tap histories: a page change, a lock, a wake.
void desk_speed_reset_taps(struct desk_speed *s);

int desk_speed_bpm(const struct desk_dial *d);
// Whether a target is live for a dial, by the rules above.
int desk_speed_target_enabled(const struct desk_speed *s, int dial, enum speed_target t);
enum speed_target desk_speed_hit(const struct desk_speed *s, int x, int y, int *dial);

#endif
