#include "desk_speed_paint.h"

#include <stdio.h>
#include <string.h>

#include "desk_layout.h"
#include "desk_speed_layout.h"
#include "speed_factor.h"

static int text_h(struct font *f) { return f ? font_height(f) : 15; }

static void text_at(struct canvas *c, struct font *f, int x, int y, int max_w, const char *s, uint32_t col) {
    if (f)
        font_draw_fit(f, c, x, y + font_baseline(f), max_w, s, col);
    else
        canvas_text(c, x, y, s, 3, col);
}

static void centred(struct canvas *c, struct font *f, int x, int y, int w, int h, const char *s, uint32_t col) {
    int width = f ? font_width(f, s) : canvas_text_width(s, 3);
    if (width > w)
        width = w;
    text_at(c, f, x + (w - width) / 2, y + (h - text_h(f)) / 2, w, s, col);
}

// A target: a tile in the desk's palette, glass and muted when dead, an
// ink outline while a finger is on it. Amber is never a target's: it is
// the master's word, and a tempo has no "on".
static void target(struct canvas *c, struct font *f, int x, int y, int w, int h,
                   const char *label, int enabled, int pressed, uint32_t fill, uint32_t dead) {
    if (!enabled)
        fill = dead;
    canvas_round_rect(c, x, y, w, h, 12, fill);
    if (pressed && enabled) {
        canvas_round_rect(c, x, y, w, h, 12, DESK_INK);
        canvas_round_rect(c, x + 3, y + 3, w - 6, h - 6, 9, fill);
    }
    centred(c, f, x, y, w, h, label, enabled ? DESK_INK : DESK_MUTED);
}

static void card(struct canvas *c, const struct desk_speed *s, int i, const struct desk_fonts *fonts) {
    const struct desk_dial *d = &s->dial[i];
    int cx = SPEED_CARD_X, cy = SPEED_CARD_Y(i);
    canvas_round_rect(c, cx, cy, SPEED_CARD_W, SPEED_CARD_H, SPEED_RADIUS, DESK_TILE);
    int rx = cx + SPEED_READOUT_X;
    // The readout: the caption, the BPM large, the time and factor beneath.
    text_at(c, fonts->small, rx, cy + 12, SPEED_READOUT_W, d->caption, DESK_MUTED);
    char big[16], line[48], scope[32];
    if (!d->enabled || !d->known) {
        snprintf(big, sizeof big, "--");
        snprintf(line, sizeof line, "%s", d->reason);
    } else if (d->base_ms == 0) {
        snprintf(big, sizeof big, "--");
        snprintf(line, sizeof line, "No base time");
    } else {
        snprintf(big, sizeof big, "%d", desk_speed_bpm(d));
        const char *factor = d->factor == SPEED_FACTOR_NONE ? "none"
                           : d->factor == SPEED_FACTOR_ZERO ? "zero" : speed_factor_name(d->factor);
        snprintf(line, sizeof line, "%d ms  Time x%s", d->base_ms, factor);
    }
    text_at(c, fonts->value, rx, cy + 30, SPEED_READOUT_W, big, d->known ? DESK_INK : DESK_MUTED);
    int big_w = fonts->value ? font_width(fonts->value, big) : canvas_text_width(big, 3);
    if (d->known && d->base_ms > 0)
        text_at(c, fonts->small, rx + big_w + 8, cy + 30 + text_h(fonts->value) - text_h(fonts->small) - 10,
                60, "BPM", DESK_MUTED);
    text_at(c, fonts->small, rx, cy + SPEED_CARD_H - 52, SPEED_READOUT_W, line, d->known ? DESK_INK : DESK_MUTED);
    if (d->note[0])
        snprintf(scope, sizeof scope, "%s", d->note);
    else if (d->pending)
        snprintf(scope, sizeof scope, "Sent, waiting");
    else
        snprintf(scope, sizeof scope, "%d functions", d->members);
    text_at(c, fonts->small, rx, cy + SPEED_CARD_H - 30, SPEED_READOUT_W, scope,
            d->note[0] ? DESK_AMBER : DESK_MUTED);

    int pressed_here = s->capture_dial == i;
    // Live targets sink into glass on the card; dead ones are text alone.
    target(c, fonts->tile, cx + SPEED_TAP_X, cy + SPEED_TAP_Y, SPEED_TAP_W, SPEED_TAP_H, "Tap",
           desk_speed_target_enabled(s, i, SPEED_T_TAP), pressed_here && s->capture == SPEED_T_TAP,
           DESK_GLASS, DESK_TILE);
    static const struct { enum speed_target t; int row, col; const char *label; } cells[] = {
        { SPEED_T_BPM_DOWN, 0, 0, "-1 BPM" }, { SPEED_T_BPM_UP, 0, 1, "+1 BPM" },
        { SPEED_T_FACTOR_ONE, 0, 2, "x1" }, { SPEED_T_HALF, 1, 0, "x1/2" }, { SPEED_T_DOUBLE, 1, 1, "x2" },
    };
    for (size_t k = 0; k < sizeof cells / sizeof cells[0]; k++)
        target(c, fonts->small, cx + SPEED_CELL_X(cells[k].col), cy + SPEED_CELL_Y(cells[k].row),
               SPEED_CELL_W, SPEED_CELL_H, cells[k].label,
               desk_speed_target_enabled(s, i, cells[k].t), pressed_here && s->capture == cells[k].t,
               DESK_GLASS, DESK_TILE);
}

void desk_speed_paint(struct canvas *c, const struct desk_speed *s, const struct desk_fonts *fonts) {
    for (int i = 0; i < s->dials && i < 2; i++)
        card(c, s, i, fonts);
    if (s->dials >= 2) {
        int enabled = desk_speed_target_enabled(s, 0, SPEED_T_BOTH);
        target(c, fonts->tile, SPEED_CARD_X, SPEED_BOTH_Y, SPEED_CARD_W, SPEED_BOTH_H,
               "Tap both", enabled, s->capture == SPEED_T_BOTH, DESK_TILE, DESK_GLASS);
    }
}
