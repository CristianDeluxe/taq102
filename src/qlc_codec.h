// The QLC+ 5.2.2 web-access wire grammar: UTF-8 text frames of pipe-separated
// fields, one command per frame. Pure: no sockets, no allocation, no clock.
// Encoders write a NUL-terminated frame and return its length, or -1 when the
// arguments or the buffer are not good for it. Decoding never trusts the frame.
#ifndef QLC_CODEC_H
#define QLC_CODEC_H

#include <stddef.h>
#include <stdint.h>

// A widget's action type as /vc.json reports it. Toggle acts on every message
// it receives, 0 included, so a toggle gesture sends exactly one.
enum qlc_action { QLC_ACTION_TOGGLE = 0, QLC_ACTION_FLASH = 1,
                  QLC_ACTION_BLACKOUT = 2, QLC_ACTION_STOP_ALL = 3 };

enum qlc_kind {
    QLC_UNKNOWN,        // anything this desk does not act on
    QLC_BUTTON,         // <wID>|BUTTON|<value>
    QLC_SLIDER,         // <wID>|SLIDER|<value>
    QLC_GRAND_MASTER,   // GM_VALUE|<value>|<display>
    QLC_FUNCTION,       // FUNCTION|<fID>|Running|Stopped
    QLC_API,            // QLC+API|<query>|<payload>
};

struct qlc_msg {
    enum qlc_kind kind;
    int widget_id;      // QLC_BUTTON, QLC_SLIDER
    int function_id;    // QLC_FUNCTION
    int value;          // QLC_BUTTON, QLC_SLIDER, QLC_GRAND_MASTER
    int running;        // QLC_FUNCTION: 1 running, 0 stopped
    char query[32];     // QLC_API: the query name, without the payload
    char payload[64];   // QLC_API: the rest of the frame, truncated to fit
};

// Returns 0 and fills out for any frame it can read, -1 for a frame it cannot.
// A frame whose shape is known but whose numbers are not is -1, never a
// message with a guessed value. len is the frame length; the frame need not be
// NUL-terminated and may contain no NUL.
int qlc_decode(const char *frame, size_t len, struct qlc_msg *out);

// One toggle gesture, one frame. The value is the state being asked for, and
// QLC+ toggles on either, so callers send this once per gesture and never
// resend it after an ambiguous disconnect.
int qlc_encode_toggle(char *buf, size_t cap, int widget_id);
// Flash needs both edges: on at contact, off at release or cancel.
int qlc_encode_flash(char *buf, size_t cap, int widget_id, int on);
// A slider widget, in its own configured range.
int qlc_encode_level(char *buf, size_t cap, int widget_id, int value);
// The grand master, 0..255.
int qlc_encode_grand_master(char *buf, size_t cap, int value);
// A speed dial, in milliseconds, before the dial applies its per-function
// factors. Negative durations are refused; QLC+ reads 0 as "infinite".
int qlc_encode_speed_ms(char *buf, size_t cap, int widget_id, int ms);
// An XY pad, from normalised 0..1 screen coordinates to the pad's own units:
// DMX coarse plus a fine fraction, 0.00 to 255.99, clamped to the widget's
// configured range. y is already top-down, as the pad's own axis is.
int qlc_encode_xy(char *buf, size_t cap, int widget_id, float x, float y,
                  float h_min, float h_max, float v_min, float v_max);
// A Click-and-Go colour slider: the RGB colour and the white/amber/UV
// companion, each 0xRRGGBB.
int qlc_encode_color(char *buf, size_t cap, int widget_id, uint32_t rgb,
                     uint32_t wauv);
// Release a slider's override of the channels it owns. Not the same as
// setting it to black.
int qlc_encode_slider_release(char *buf, size_t cap, int widget_id);

#endif
