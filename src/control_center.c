#include <math.h>
#include <string.h>
#include "control_center.h"
#include "control_center_layout.h"

enum capture { CAP_NONE, CAP_HANDLE, CAP_WIFI, CAP_BRIGHT, CAP_AUTO,
    CAP_OFF, CAP_RESCUE, CAP_LOADER, CAP_TIMER1, CAP_TIMER5, CAP_TIMER15,
    CAP_TIMER_NEVER, CAP_OUTSIDE };

static int elapsed(int64_t now, int64_t then, int64_t limit) {
    return now >= then && (uint64_t)now - (uint64_t)then >= (uint64_t)limit;
}
static int inside(struct cc_rect r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static int hit(float x, float y) {
    if (inside(CC_HANDLE, x, y)) return CAP_HANDLE;
    if (inside(CC_WIFI, x, y)) return CAP_WIFI;
    if (inside(CC_BRIGHT, x, y)) return CAP_BRIGHT;
    if (inside(CC_AUTO, x, y)) return CAP_AUTO;
    if (inside(CC_OFFNOW, x, y)) return CAP_OFF;
    if (inside(CC_RESCUE, x, y)) return CAP_RESCUE;
    if (inside(CC_LOADER, x, y)) return CAP_LOADER;
    for (int i = 0; i < 4; i++)
        if (inside(CC_TIMER_SEGMENT(i), x, y)) return CAP_TIMER1 + i;
    return inside(CC_PANEL, x, y) ? CAP_NONE : CAP_OUTSIDE;
}
static void release(struct cc_model *m) {
    m->capture_slot = -1; m->capture_tile = CAP_NONE; m->dragging_brightness = 0;
}
static void disarm(struct cc_model *m) {
    if (m->armed) { m->armed = 0; m->dirty = 1; }
}
static void slide_tick(struct cc_model *m, int64_t now) {
    float progress = elapsed(now, m->slide_start_ms, 180) ? 1.f :
        now > m->slide_start_ms ? (float)(now - m->slide_start_ms) / 180.f : 0.f;
    float eased = 1.f - (1.f - progress) * (1.f - progress) * (1.f - progress);
    m->slide = m->slide_from + ((m->open ? 1.f : 0.f) - m->slide_from) * eased;
}
void cc_init(struct cc_model *m, int brightness, int maximum, int automatic, int minutes) {
    memset(m, 0, sizeof *m);
    m->brightness_max = maximum < 8 ? 8 : maximum;
    m->brightness = brightness < 8 ? 8 : brightness > m->brightness_max ? m->brightness_max : brightness;
    m->pending_brightness = m->brightness; m->auto_on = !!automatic;
    m->sleep_minutes = minutes == 0 || minutes == 1 || minutes == 5 || minutes == 15 ? minutes : 5;
    m->capture_slot = -1; m->dirty = 1;
}
void cc_open(struct cc_model *m, int64_t now) {
    if (m->open) return;
    slide_tick(m, now); m->slide_from = m->slide; m->slide_start_ms = now; m->open = 1;
}
void cc_close(struct cc_model *m, int64_t now) {
    disarm(m); release(m);
    if (!m->open) return;
    slide_tick(m, now); m->slide_from = m->slide; m->slide_start_ms = now; m->open = 0;
}
int cc_visible(const struct cc_model *m) { return m->open || m->slide > 0.f; }
void cc_tick(struct cc_model *m, int64_t now) {
    slide_tick(m, now);
    if (m->armed && elapsed(now, m->armed_at_ms, 3000)) disarm(m);
}
float cc_slide_offset(const struct cc_model *m) { return (1.f - m->slide) * CC_SLIDE_DISTANCE; }
static void emit(struct cc_result *out, int *n, enum cc_action action, int arg) {
    out[(*n)++] = (struct cc_result){action, arg};
}
static int level(const struct cc_model *m, float y) {
    double part = (CC_BRIGHT.y + CC_BRIGHT.h - 1.0 - y) / (CC_BRIGHT.h - 1.0);
    if (part < 0) part = 0;
    if (part > 1) part = 1;
    return 8 + (int)lround(part * (m->brightness_max - 8));
}
static void brightness(struct cc_model *m, int64_t now, struct cc_result *r, int *n) {
    if (m->pending_brightness == m->brightness) return;
    m->brightness = m->pending_brightness; m->last_drag_paint_ms = now; m->dirty = 1;
    emit(r, n, CC_SET_BRIGHTNESS, m->brightness);
}
static int handle(struct cc_model *m, const struct touch_event *e, int64_t now, struct cc_result *r) {
    int n = 0;
    if (!cc_visible(m) || !isfinite(e->x) || !isfinite(e->y)) return 0;
    if (e->kind == TOUCH_DOWN) {
        if (m->capture_slot >= 0) return 0;
        m->capture_slot = e->slot; m->capture_tile = hit(e->x, e->y);
        m->capture_y = e->y; m->capture_at_ms = now;
        if (m->capture_tile == CAP_BRIGHT) {
            m->dragging_brightness = 1;
            if (m->auto_on) { m->auto_on = 0; m->dirty = 1; emit(r, &n, CC_SET_AUTO, 0); }
            m->pending_brightness = level(m, e->y); brightness(m, now, r, &n);
            m->last_drag_paint_ms = now;
        }
        return n;
    }
    if (m->capture_slot < 0 || e->slot != m->capture_slot) return 0;
    if (e->kind == TOUCH_CANCEL) { release(m); return 0; }
    if (m->dragging_brightness) {
        m->pending_brightness = level(m, e->y);
        if (e->kind == TOUCH_UP || elapsed(now, m->last_drag_paint_ms, 33)) brightness(m, now, r, &n);
        if (e->kind == TOUCH_UP) { emit(r, &n, CC_SAVE_SETTINGS, 0); release(m); }
        return n;
    }
    if (m->capture_tile == CAP_HANDLE && m->capture_y - e->y > 60) {
        cc_close(m, now); return 0;
    }
    if (e->kind != TOUCH_UP) return 0;
    int tile = m->capture_tile;
    int tapped = now >= m->capture_at_ms && !elapsed(now, m->capture_at_ms, 401) && tile == hit(e->x, e->y);
    release(m);
    if (tile == CAP_OUTSIDE) { cc_close(m, now); return 0; }
    if (!tapped) return 0;
    if (tile == CAP_AUTO) {
        m->auto_on = !m->auto_on; m->dirty = 1;
        emit(r, &n, CC_SET_AUTO, m->auto_on); emit(r, &n, CC_SAVE_SETTINGS, 0);
    } else if (tile >= CAP_TIMER1 && tile <= CAP_TIMER_NEVER) {
        const int minutes[] = {1, 5, 15, 0};
        m->sleep_minutes = minutes[tile - CAP_TIMER1]; m->dirty = 1;
        emit(r, &n, CC_SET_SLEEP_MINUTES, m->sleep_minutes); emit(r, &n, CC_SAVE_SETTINGS, 0);
    } else if (tile == CAP_WIFI) {
        int wanted = m->wifi == WIFI_OFF || m->wifi == WIFI_FAILED;
        m->wifi = wanted ? WIFI_STARTING : WIFI_OFF; m->dirty = 1;
        emit(r, &n, CC_WIFI_TOGGLE, wanted);
    }
    else if (tile == CAP_OFF) { cc_close(m, now); emit(r, &n, CC_SLEEP_NOW, 0); }
    else if (tile == CAP_RESCUE || tile == CAP_LOADER) {
        int arm = tile == CAP_RESCUE ? 1 : 2;
        if (m->armed == arm && now >= m->armed_at_ms && !elapsed(now, m->armed_at_ms, 3000)) {
            disarm(m); emit(r, &n, arm == 1 ? CC_REBOOT_RESCUE : CC_REBOOT_LOADER, 0);
        } else { m->armed = arm; m->armed_at_ms = now; m->dirty = 1; }
    }
    return n;
}
int cc_handle(struct cc_model *m, const struct touch_event *ev, int64_t now, struct cc_result *out, int max) {
    struct cc_model next = *m; struct cc_result pending[3];
    int n = handle(&next, ev, now, pending);
    if (n > max || (n && !out)) return -n;
    if (n) memcpy(out, pending, (size_t)n * sizeof *out);
    *m = next; return n;
}
