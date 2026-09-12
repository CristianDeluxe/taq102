// The Virtual Console as QLC+ 5.2.2 serves it at GET /vc.json: the widgets of
// the workspace the master has actually loaded. The desk reads this once per
// connection and validates its show map against it, so a map that no longer
// matches the loaded show disables its controls instead of firing the wrong
// cue. Parsing only; nothing here opens a socket.
#ifndef VCJSON_H
#define VCJSON_H

#include <stddef.h>

#define VC_CAPTION_MAX 48

// One widget, flattened out of the page tree. Pages themselves are not
// widgets: a page is a frame carrying its own id in a separate namespace, and
// in this show the single page and the XY pad are both id 0. `parent_id` is
// the frame, solo frame or page holding the widget. `function_id` is -1 when
// the widget drives no function; 0 is a real function id in this show and
// never means "none". `action_type` is 0 Toggle, 1 Flash, 2 Blackout, 3 Stop all, and is
// only meaningful for a button.
struct vc_widget {
    int id;
    int type_id;
    int parent_id;
    int function_id;
    int action_type;
    int page;
    char caption[VC_CAPTION_MAX];

    // Live state, as the master had it when it served the document. This is
    // what a reconnecting desk synchronises from: the socket sends no
    // snapshot of its own, and a query reply does not say which function it
    // is about, so the console's own description is the honest source.
    int visible;
    int disabled;
    int state;          // button: pressed or latched
    int value;          // slider: its current level
    int monitor;        // slider: it follows the channels it owns
    int overriding;     // slider: it currently owns them
    char cng_type[16];  // slider: its Click-and-Go mode, "None" when plain

    // XY pad: where the master has it, and the window it may travel in. The
    // range is in DMX coarse units, and a saved window of zero width still
    // leaves the fine byte free, so it restricts travel rather than
    // forbidding it.
    float pos_x, pos_y;
    float h_min, h_max, v_min, v_max;

    // Speed dial: the time it shows, the factor enum it multiplies by, and
    // the range its own control allows (the engine does not clamp a time
    // sent over the socket, so the desk keeps to it).
    int speed_ms;
    int speed_factor;
    int speed_min_ms, speed_max_ms;   // -1 when the document has none
};

struct vc_doc {
    char app_version[16];
    int page_count;
    int count;
    struct vc_widget *widget;   // owned, `count` long, ascending by id
};

// Widget type ids, as /vc.json reports them. The `type` string beside them is
// translated into the master's locale and must never be compared against.
enum vc_type {
    VC_BUTTON = 1, VC_SLIDER = 2, VC_XYPAD = 3, VC_FRAME = 4,
    VC_SOLO_FRAME = 5, VC_SPEED_DIAL = 6, VC_LABEL = 8,
    VC_AUDIO_TRIGGERS = 9, VC_ANIMATION = 10,
};

enum vc_action { VC_TOGGLE = 0, VC_FLASH = 1, VC_BLACKOUT = 2, VC_STOP_ALL = 3 };

// Parses `len` bytes of JSON. Returns 0 on success, -1 on anything else: not
// JSON, not a Virtual Console, no widgets, a widget without a usable id, or
// two widgets sharing one id. On failure nothing is allocated and `out` is
// cleared. The document is the master's word about itself, not a trusted
// input: every string is bounded and every number is range-checked.
int vc_parse(const char *json, size_t len, struct vc_doc *out);
void vc_free(struct vc_doc *doc);

// Binary search by widget id. NULL when the loaded console has no such widget,
// which is how a stale show map is caught.
const struct vc_widget *vc_find(const struct vc_doc *doc, int id);

#endif
