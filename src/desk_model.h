// The desk's state and what a touch does to it. Pure: no sockets, no clock of
// its own, no drawing. Everything that reaches the show leaves here as an
// action for a caller to send, and everything the show says comes back in
// through desk_apply_*, so the tiles show the rig rather than the finger.
//
// A control is a fact about the show; where it is drawn is a placement, and
// the model holds the layout of every page and the view (page, bank) that is
// on screen. A touch is resolved against the placements of that view only.
#ifndef DESK_MODEL_H
#define DESK_MODEL_H

#include <stdint.h>

#include "desk_layout_resolve.h"
#include "showmap.h"

#define DESK_MAX_CONTROLS (MAP_MAX_CONTROLS + 4)
#define DESK_LABEL_MAX 48
#define DESK_REASON_MAX 48
#define DESK_MAX_SWATCHES MAP_MAX_SWATCHES

enum desk_kind {
    DESK_CUE,        // a Toggle button in the console
    DESK_MASTER,     // the grand master
    DESK_STOP_ALL,   // the console's own StopAll: the panic button
    DESK_HOLD,       // a Flash button fired while the finger is down (the hold model's)
    DESK_BURST,      // a SingleShot chaser started on contact, stopped on release, ended by the master
    DESK_HAZE_OFF,   // SHOW's ambient OFF: stops whichever haze rhythm runs
    DESK_TEMPO,      // SHOW's tempo card: the show dial's BPM and a tap (the speed model's)
};

// A control's state is three-valued on purpose. Until the desk has been told,
// it does not know, and it says so instead of drawing "off".
enum desk_state { DESK_UNKNOWN, DESK_OFF, DESK_ON };

enum desk_link { DESK_LINK_DOWN, DESK_LINK_CONNECTING, DESK_LINK_SYNCING,
                 DESK_LINK_READY };

struct desk_control {
    enum desk_kind kind;
    char label[DESK_LABEL_MAX];
    char detail[DESK_LABEL_MAX];    // the second line, when the map has one
    int widget_id;      // -1 when the control has no console widget
    int function_id;    // -1 when it drives no function
    int page, section;  // where the map puts it; -1 for the chrome controls
    enum map_role role;
    uint32_t swatch[DESK_MAX_SWATCHES];   // 0xAARRGGBB
    int swatches;
    enum desk_state state;
    int level;          // DESK_MASTER: 0..255, the master's own word
    int requested_level;// DESK_MASTER: what the finger asked for; never painted as the rig's
    int enabled;
    char reason[DESK_REASON_MAX];   // why it is disabled, shown in its place
    int pressed;        // a finger is on it now: local feedback only
    int pending;        // its frame went out; the master has not answered yet
    int64_t pending_since;
    int hold_progress;  // DESK_HOLD, DESK_BURST: 0..1000 of the cap used while down, -1 idle
    int hold_index;     // DESK_HOLD, DESK_BURST: the hold model's index, -1 when it has none
    int burst_ms;       // DESK_BURST: the master's own length
};

#define DESK_PENDING_MS 1500

struct desk_model {
    struct desk_control control[DESK_MAX_CONTROLS];
    int count;
    struct desk_layout layout;
    int page, bank;         // the view on screen
    int page_bank[MAP_MAX_PAGES]; // each page resumes at its last bank
    enum desk_link link;
    int locked;             // the surface accepts nothing; painted as such
    int mismatch;           // the console is another show: every control off, said once
    char master_name[64];   // what the desk is talking to, for the bar
    int battery, charging, wifi_bars;   // the bar's status, fed by the caller
    int setup_open;                     // the gear reads as pressed
    int dirty;
    // What changed since the last paint: a rectangle in panel pixels, or
    // everything. A full repaint costs a fifth of a second on the tablet's
    // CPU; a tile costs a few milliseconds.
    int damage_x, damage_y, damage_w, damage_h;
    int damage_all;
    // One capture at a time: the slot is remembered so a second finger cannot
    // fire another tile, and so a cancel can only undo its own gesture. The
    // placement is remembered too, since the gesture's geometry is its own.
    int capture_slot;
    int capture_index;
    int capture_placement;
    // The panic button is the one exception: a second finger reaches it
    // while a cue is held or the fader dragged, and it fires on release.
    int panic_slot;
    int panic_placement;
};

enum desk_action_kind { DESK_ACT_NONE, DESK_ACT_TOGGLE, DESK_ACT_MASTER, DESK_ACT_STOP_ALL };

struct desk_action {
    enum desk_action_kind kind;
    int widget_id;
    int value;          // DESK_ACT_MASTER: 0..255
};

void desk_init(struct desk_model *m);

// Returns the index of the control added, or -1 when there is no room.
int desk_add(struct desk_model *m, const struct desk_control *control);

// A new layout, copied in; the view and every remembered bank reset to zero.
void desk_set_layout(struct desk_model *m, const struct desk_layout *layout);
// Changes the view. A capture in flight is cancelled: a finger that came down
// on one page fires nothing on another. Bank -1 recalls that page's last
// bank; other out-of-range values are clamped.
void desk_set_view(struct desk_model *m, int page, int bank);

// The placement a control has on the current view, or NULL.
const struct desk_placement *desk_placement_of(const struct desk_model *m, int control);
// The control under a point on the current view, or -1: for the caller's
// own routing of holds and the tempo card, which the model does not capture.
int desk_control_at(const struct desk_model *m, int x, int y);
// A hold's progress for the painter; damages its tile when it moved.
void desk_set_hold_progress(struct desk_model *m, int control, int progress, int pressed);

// A contact going down, moving and coming up. Coordinates are panel pixels.
// At most one action comes out of one gesture, and it comes out on release:
// a Toggle that fired on contact would fire again on the next redraw of the
// operator's mind, and a release that never arrives must not leave the show
// changed. `slot` identifies the finger; a second finger is ignored while one
// is captured.
struct desk_action desk_touch_down(struct desk_model *m, int slot, int x, int y);
struct desk_action desk_touch_move(struct desk_model *m, int slot, int x, int y);
struct desk_action desk_touch_up(struct desk_model *m, int slot, int x, int y);
void desk_touch_cancel(struct desk_model *m, int slot);
// Drops any capture, whatever finger holds it: a display or lock transition
// must not deliver a gesture that started on the other side of it.
void desk_cancel_all(struct desk_model *m);
void desk_set_locked(struct desk_model *m, int locked);
// The bar's facts: battery 0..100 (-1 unknown), charging, Wi-Fi bars 0..3,
// whether the settings surface is open. Damages the bar when any changed.
void desk_set_status(struct desk_model *m, int battery, int charging, int wifi_bars, int setup_open);

// Adds a rectangle to the damage (the status bar, say, which the model does
// not know about).
void desk_damage_rect(struct desk_model *m, int x, int y, int w, int h);
// Whether anything needs painting, and what: returns 1 and the rectangle
// (the whole screen when everything changed), clearing the damage and the
// dirty flag. 0 when nothing changed.
int desk_take_damage(struct desk_model *m, int *x, int *y, int *w, int *h);

// The frame for `widget_id` went out: the control shows a neutral pending
// mark and refuses a second tap until the master answers (any push for its
// function) or DESK_PENDING_MS pass. Never painted amber, never retried.
void desk_note_sent(struct desk_model *m, int widget_id, int64_t now_ms);
void desk_tick(struct desk_model *m, int64_t now_ms);

// State from the master. A function id can drive more than one control.
void desk_apply_function(struct desk_model *m, int function_id, int running);
void desk_apply_master(struct desk_model *m, int value);
// Link changes clear what the desk cannot vouch for: on anything below ready,
// every control's state goes back to unknown and captures are dropped.
void desk_set_link(struct desk_model *m, enum desk_link link);

#endif
