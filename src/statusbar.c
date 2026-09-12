#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "canvas_blend.h"
#include "font.h"
#include "statusbar.h"


// The charging bolt as the six-point polygon iOS draws, in a unit box with y
// running down. It replaced a 5-by-7 bitmap: scaled up to the icon's height
// that bitmap was the one shape in the bar with visible steps, because the
// supersampling that smooths everything else can only average what the
// geometry already describes.
static const float BOLT[6][2] = {
    { 0.62f, 0.00f }, { 0.20f, 0.52f }, { 0.45f, 0.52f },
    { 0.38f, 1.00f }, { 0.80f, 0.46f }, { 0.55f, 0.46f },
};

// Even-odd ray cast, the standard point-in-polygon test: count the edges a
// ray to the left crosses.
static int in_bolt(float x, float y) {
    int inside = 0;
    for (int i = 0, j = 5; i < 6; j = i++) {
        float xi = BOLT[i][0], yi = BOLT[i][1];
        float xj = BOLT[j][0], yj = BOLT[j][1];
        if ((yi > y) != (yj > y) &&
            x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

// The Wi-Fi fan: a dot and three arcs of a 90-degree sector opening upward
// from the apex at (ax, ay). `lit` arcs from the inside out take ink, the
// rest dim, as iOS greys out the bars it does not have.
//
// With no link at all -- `lit` is zero only when there is no interface or no
// address -- the whole fan goes dim and takes a slash, which is what iOS shows
// rather than an all-grey fan that reads as "one bar, far away". The stroke
// runs at 45 degrees through the middle of the glyph and carries a clear gap
// either side, so it reads over the arcs instead of merging into them.
static void wifi_fan(struct canvas *c, int ax, int ay, int size, int lit, const struct statusbar_style *sty) {
    float unit = size / 4.f, thick = unit * 0.45f;
    int slashed = lit <= 0;
    // A point on the stroke, and the half-widths of the stroke and its gap.
    float sx = ax, sy = ay - size / 2.f;
    float half = thick * 0.5f, gap = thick;
    for (int y = ay - size; y <= ay; y++)
        for (int x = ax - size; x <= ax + size; x++) {
            float dx = x - ax, dy = ay - y;
            // Distance to the stroke, which the loop's own bounds clip to the
            // glyph: at the top row it sits at ax - size/2, at the apex row at
            // ax + size/2.
            float sd = slashed ? fabsf(((x - sx) - (y - sy)) * 0.70710678f) : 0.f;
            if (slashed && sd <= half) { canvas_put(c, x, y, sty->ink); continue; }
            // 55 degrees each side, not 45: the narrower sector drew a fan
            // that came to a point, where the iPhone's is wide and shallow.
            if (dy < 0 || fabsf(dx) > dy * 1.43f) continue;
            float r = sqrtf(dx * dx + dy * dy);
            int ring = -1;
            if (r <= unit * 0.55f) ring = 0;
            else for (int k = 1; k <= 3; k++)
                if (r <= unit * (k + 0.5f) && r > unit * (k + 0.5f) - thick) ring = k;
            if (ring < 0) continue;
            if (slashed && sd <= half + gap) continue;
            canvas_put(c, x, y, slashed ? sty->dim : ring <= lit ? sty->ink : sty->dim);
        }
}

// The battery: an outline with a nub, the charge as a fill, and a bolt
// while current flows in. (x, y) is the top-left of the body.
// `known` is whether the driver reported a capacity at all: without one the
// outline stands empty, with no fill and no bolt, rather than claiming 0%.
static void battery_icon(struct canvas *c, int x, int y, int w, int h, int cap, int charging, int known, const struct statusbar_style *sty) {
    int r = h / 3, line = h / 10 > 1 ? h / 10 : 1, gap = line;
    int inner_r = r - line > 0 ? r - line : 1;
    canvas_round_rect(c, x, y, w, h, r, sty->ink);
    canvas_round_rect(c, x + line, y + line, w - 2 * line, h - 2 * line, inner_r, sty->hollow);
    canvas_fill_rect(c, x + w, y + h / 3, line + 1, h / 3, sty->ink);           // the nub
    int inner_w = w - 2 * (line + gap), inner_h = h - 2 * (line + gap);
    int fill_w = known ? inner_w * (cap < 0 ? 0 : cap > 100 ? 100 : cap) / 100 : 0;
    uint32_t col = charging ? sty->charge : cap <= 20 ? sty->low : sty->ink;
    if (fill_w > 0)
        canvas_round_rect(c, x + line + gap, y + line + gap, fill_w, inner_h, inner_r, col);
    if (charging && known) {
        // Sized to the cell, not to a glyph grid: the bolt stands as tall as
        // the fill it sits on, less a hair of margin.
        int bh = inner_h * 6 / 7, bw = bh * 3 / 5;
        if (bh < 1) bh = 1;
        if (bw < 1) bw = 1;
        int bx = x + w / 2 - bw / 2, by = y + h / 2 - bh / 2;
        for (int py = 0; py < bh; py++)
            for (int px = 0; px < bw; px++)
                if (in_bolt((px + 0.5f) / bw, (py + 0.5f) / bh))
                    canvas_put(c, bx + px, by + py, sty->pale);
    }
}

// Everything is laid out in units of s, one 256th of the width: text 7
// units tall, the battery 13 by 7, the bar 12. Painted at four times that
// scale and averaged down, so the curves and the diagonals of the glyphs do
// not show the blocks they are built from.
#define UNIT(w) ((w) / 256)
#define SS 4

int statusbar_height(int w) { return 12 * UNIT(w); }

static struct font *bar_font;
static char *bar_font_path;

static void close_bar_font(void) {
    font_close(bar_font);
    free(bar_font_path);
    bar_font = NULL;
    bar_font_path = NULL;
}

static struct font *get_bar_font(const char *path) {
    if (!path) return NULL;
    if (bar_font_path && strcmp(path, bar_font_path) == 0) return bar_font;
    static int cleanup_registered;
    if (!cleanup_registered) {
        if (atexit(close_bar_font) != 0) return NULL;
        cleanup_registered = 1;
    }
    close_bar_font();
    bar_font_path = strdup(path);
    if (!bar_font_path) return NULL;
    // Match the canvas supersampling; cache failed paths as well as open faces.
    bar_font = font_open(path, 18 * SS);
    printf("statusbar text: %s (%s)\n", bar_font ? "Inter" : "bitmap fallback", path);
    return bar_font;
}

static void paint_at(struct canvas *c, const struct status *st, const struct statusbar_style *sty, int s) {
    int bar_h = 12 * s;
    // The panel's edge sits under the bezel by a few millimetres: measured
    // 2026-09-04, the battery's nub was cut off at a 3-unit margin.
    int margin = 12 * s;
    canvas_fill_rect(c, 0, 0, c->w, bar_h, sty->bar);
    // iOS keeps the battery close to the height of the type beside it and
    // about 2.15 times as wide as it is tall. At 7 by 13 units against 18-pixel
    // digits the icons were half again too tall and too stubby, which is what
    // made them read as someone else's status bar. 5 by 11 is the iPhone's
    // ratio at this bar's scale.
    int icon_h = 5 * s, icon_w = 11 * s;
    int y = (bar_h - icon_h) / 2;
    int right = c->w - margin - icon_w - s - 1;
    // iOS: plugged in is the bolt and the green, whatever the current does;
    // the rescue screen's text line still prints the milliamps.
    battery_icon(c, right, y, icon_w, icon_h, st->cap, st->plugged, st->have_batt, sty);
    char pct[8];
    if (st->have_batt)
        snprintf(pct, sizeof pct, "%d%%", st->cap);
    else
        snprintf(pct, sizeof pct, "--%%");
    struct font *f = get_bar_font(sty->font_path);
    // The 5x7 face has digits and the percent sign only; the unknown mark
    // falls back to the 3x5 face, which has the dash.
    int seven = f == NULL && st->have_batt;
    right -= 2 * s + (f ? font_width(f, pct) : seven ? canvas_text7_width(pct, s) : canvas_text_width(pct, s));
    if (f) {
        int baseline = (bar_h - font_height(f)) / 2 + font_baseline(f);
        font_draw(f, c, right, baseline, pct, sty->ink);
    } else if (seven) {
        canvas_text7(c, right, (bar_h - 7 * s) / 2, pct, s, sty->ink);
    } else {
        canvas_text(c, right, (bar_h - 5 * s) / 2, pct, s, sty->ink);
    }
    right -= 3 * s + icon_h;
    wifi_fan(c, right, y + icon_h, icon_h, status_wifi_bars(st), sty);
}

// Average SS*SS supersampled pixels into one, alpha-weighted, and lay the
// result over what the destination already holds. Only the bar's rows are
// touched: the rescue screen hands over its whole amber canvas, and until
// 2026-09-07 every pixel outside the bar came back as 0 -- black with alpha
// 0, which the stock VOP blends away into a washed-out panel (the v43
// round-trip photograph). glcube's bar canvas is bar-sized and starts at 0,
// so for it "over" is the plain copy it always was.
static void downsample(struct canvas *dst, const struct canvas *src, int rows) {
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < dst->w; x++) {
            unsigned a = 0, r = 0, g = 0, b = 0;
            for (int j = 0; j < SS; j++)
                for (int i = 0; i < SS; i++) {
                    uint32_t p = src->px[(y * SS + j) * src->w + x * SS + i];
                    unsigned pa = p >> 24;
                    a += pa;
                    r += ((p >> 16) & 0xff) * pa;
                    g += ((p >> 8) & 0xff) * pa;
                    b += (p & 0xff) * pa;
                }
            if (!a) continue;
            unsigned sa = a / (SS * SS), sr = r / a, sg = g / a, sb = b / a;
            canvas_blend(dst, x, y, (sa << 24) | (sr << 16) | (sg << 8) | sb);
        }
}

void statusbar_paint(struct canvas *c, const struct status *st, const struct statusbar_style *sty) {
    int rows = statusbar_height(c->w);
    if (rows > c->h) rows = c->h;
    struct canvas big = { calloc((size_t)c->w * SS * rows * SS, 4), c->w * SS, rows * SS };
    if (!big.px) return;
    paint_at(&big, st, sty, UNIT(c->w) * SS);
    downsample(c, &big, rows);
    free(big.px);
}
