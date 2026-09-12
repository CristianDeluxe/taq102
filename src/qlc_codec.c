#include "qlc_codec.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// A view of one pipe-separated field of a frame. The frame is not
// NUL-terminated and its bytes are never modified.
struct field { const char *p; size_t n; };

static int next_field(const char *frame, size_t len, size_t *pos,
                      struct field *out) {
    out->p = frame;
    out->n = 0;
    if (*pos > len)
        return -1;
    const char *start = frame + *pos;
    size_t left = len - *pos;
    const char *bar = memchr(start, '|', left);
    out->p = start;
    out->n = bar ? (size_t)(bar - start) : left;
    *pos += out->n + (bar ? 1 : 0);
    return bar ? 1 : 0;      // 1: another field follows, 0: this was the last
}

static int field_is(const struct field *f, const char *word) {
    size_t n = strlen(word);
    return f->n == n && memcmp(f->p, word, n) == 0;
}

// Digits only, no sign, no space, within [min, max]. Anything else is -1.
static int field_int(const struct field *f, int min, int max, int *out) {
    if (f->n == 0 || f->n > 9)
        return -1;
    int v = 0;
    for (size_t i = 0; i < f->n; i++) {
        if (f->p[i] < '0' || f->p[i] > '9')
            return -1;
        v = v * 10 + (f->p[i] - '0');
    }
    if (v < min || v > max)
        return -1;
    *out = v;
    return 0;
}

static void field_copy(const struct field *f, char *dst, size_t cap) {
    size_t n = f->n < cap - 1 ? f->n : cap - 1;
    memcpy(dst, f->p, n);
    dst[n] = '\0';
}

int qlc_decode(const char *frame, size_t len, struct qlc_msg *out) {
    if (!frame || !out || len == 0 || memchr(frame, '\0', len))
        return -1;

    memset(out, 0, sizeof *out);
    size_t pos = 0;
    struct field head = { frame, 0 };
    int more = next_field(frame, len, &pos, &head);

    if (field_is(&head, "FUNCTION")) {
        struct field id = { frame, 0 }, state = { frame, 0 };
        if (more != 1 || next_field(frame, len, &pos, &id) != 1)
            return -1;
        next_field(frame, len, &pos, &state);
        if (field_int(&id, 0, 0x7FFFFFF, &out->function_id) != 0)
            return -1;
        if (field_is(&state, "Running"))
            out->running = 1;
        else if (field_is(&state, "Stopped"))
            out->running = 0;
        else
            return -1;
        out->kind = QLC_FUNCTION;
        return 0;
    }

    if (field_is(&head, "GM_VALUE")) {
        struct field value = { frame, 0 };
        if (more != 1)
            return -1;
        next_field(frame, len, &pos, &value);
        if (field_int(&value, 0, 255, &out->value) != 0)
            return -1;
        out->kind = QLC_GRAND_MASTER;
        return 0;
    }

    if (field_is(&head, "QLC+API")) {
        struct field query = { frame, 0 };
        if (more != 1)
            return -1;
        more = next_field(frame, len, &pos, &query);
        field_copy(&query, out->query, sizeof out->query);
        // The payload is the rest of the frame, pipes and all.
        struct field rest = { frame + pos, more == 1 ? len - pos : 0 };
        field_copy(&rest, out->payload, sizeof out->payload);
        out->kind = QLC_API;
        return 0;
    }

    // Everything else is addressed to a widget: an id, a type, and whatever
    // that type carries. A type this desk does not act on is not an error.
    if (field_int(&head, 0, 0x7FFFFFF, &out->widget_id) != 0 || more != 1)
        return -1;
    struct field type = { frame, 0 };
    more = next_field(frame, len, &pos, &type);
    if (field_is(&type, "BUTTON") || field_is(&type, "SLIDER")) {
        struct field value = { frame, 0 };
        if (more != 1)
            return -1;
        next_field(frame, len, &pos, &value);
        if (field_int(&value, 0, 255, &out->value) != 0)
            return -1;
        out->kind = field_is(&type, "BUTTON") ? QLC_BUTTON : QLC_SLIDER;
        return 0;
    }
    out->kind = QLC_UNKNOWN;
    return 0;
}

// Every encoder ends here, so the truncation rule is written once.
static int emit(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap)
        return -1;
    return n;
}

static int widget_ok(int widget_id) { return widget_id >= 0; }

int qlc_encode_toggle(char *buf, size_t cap, int widget_id) {
    if (!widget_ok(widget_id))
        return -1;
    return emit(buf, cap, "%d|255", widget_id);
}

int qlc_encode_flash(char *buf, size_t cap, int widget_id, int on) {
    if (!widget_ok(widget_id))
        return -1;
    return emit(buf, cap, "%d|%d", widget_id, on ? 255 : 0);
}

int qlc_encode_level(char *buf, size_t cap, int widget_id, int value) {
    if (!widget_ok(widget_id) || value < 0 || value > 255)
        return -1;
    return emit(buf, cap, "%d|%d", widget_id, value);
}

int qlc_encode_grand_master(char *buf, size_t cap, int value) {
    if (value < 0 || value > 255)
        return -1;
    return emit(buf, cap, "GM_VALUE|%d", value);
}

int qlc_encode_speed_ms(char *buf, size_t cap, int widget_id, int ms) {
    if (!widget_ok(widget_id) || ms < 0)
        return -1;
    return emit(buf, cap, "%d|SPEED_TIME|%d", widget_id, ms);
}

int qlc_encode_xy(char *buf, size_t cap, int widget_id, float x, float y,
                  float h_min, float h_max, float v_min, float v_max) {
    if (!widget_ok(widget_id) || isnan(x) || isnan(y))
        return -1;
    if (!(h_min <= h_max) || !(v_min <= v_max))
        return -1;
    if (h_min < 0.0f || v_min < 0.0f || h_max > 255.996f || v_max > 255.996f)
        return -1;
    float hx = x * 255.0f, vy = y * 255.0f;
    hx = hx < h_min ? h_min : (hx > h_max ? h_max : hx);
    vy = vy < v_min ? v_min : (vy > v_max ? v_max : vy);
    return emit(buf, cap, "%d|XYPAD|%.2f|%.2f", widget_id, (double)hx,
                (double)vy);
}

int qlc_encode_color(char *buf, size_t cap, int widget_id, uint32_t rgb,
                     uint32_t wauv) {
    if (!widget_ok(widget_id) || rgb > 0xFFFFFF || wauv > 0xFFFFFF)
        return -1;
    return emit(buf, cap, "%d|CNG_COLORS|#%06x|#%06x", widget_id, rgb, wauv);
}

int qlc_encode_slider_release(char *buf, size_t cap, int widget_id) {
    if (!widget_ok(widget_id))
        return -1;
    return emit(buf, cap, "%d|SLIDER_OVERRIDE|0", widget_id);
}
