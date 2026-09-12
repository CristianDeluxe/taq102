// The desk's state and what a touch does to it. Pure: no sockets, no clock of
// its own, no drawing. Everything that reaches the show leaves here as an
// action for a caller to send, and everything the show says comes back in
// through desk_apply_*, so the tiles show the rig rather than the finger.
#ifndef DESK_MODEL_H
#define DESK_MODEL_H

#include <stdint.h>

#include "showmap.h"

#define DESK_MAX_CONTROLS (MAP_MAX_CONTROLS + 2)
#define DESK_LABEL_MAX 48
#define DESK_REASON_MAX 48
#define DESK_MAX_SWATCHES MAP_MAX_SWATCHES

enum desk_kind {
    DESK_CUE,        // a Toggle button in the console
    DESK_MASTER,     // the grand master
    DESK_STOP_ALL,   // the console's own StopAll: the panic button
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
    int x, y, w, h;     // a zero size is a control with no place on screen yet
    enum desk_state state;
    int level;          // DESK_MASTER: 0..255, the master's own word
    int requested_level;// DESK_MASTER: what the finger asked for; never painted as the rig's
    int enabled;
    char reason[DESK_REASON_MAX];   // why it is disabled, shown in its place
    int pressed;        // a finger is on it now: local feedback only
};

struct desk_model {
    struct desk_control control[DESK_MAX_CONTROLS];
    int count;
    enum desk_link link;
    char master_name[64];   // what the desk is talking to, for the rail
    int dirty;
    // One capture at a time: the slot is remembered so a second finger cannot
    // fire another tile, and so a cancel can only undo its own gesture.
    int capture_slot;
    int capture_index;
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

// State from the master. A function id can drive more than one control.
void desk_apply_function(struct desk_model *m, int function_id, int running);
void desk_apply_master(struct desk_model *m, int value);
// Link changes clear what the desk cannot vouch for: on anything below ready,
// every control's state goes back to unknown and captures are dropped.
void desk_set_link(struct desk_model *m, enum desk_link link);

#endif
