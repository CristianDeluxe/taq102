#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "statusbar.h"

#define GREEN 0xFF30C048u   // iOS charging green
#define RED   0xFFE03030u   // iOS low-battery red

// The charging bolt, 5 wide by 7 tall, bit 4 the left column.
static const unsigned char BOLT[7] = { 0x03, 0x06, 0x0C, 0x1F, 0x06, 0x0C, 0x18 };

// The Wi-Fi fan: a dot and three arcs of a 90-degree sector opening upward
// from the apex at (ax, ay). `lit` arcs from the inside out take ink, the
// rest dim, as iOS greys out the bars it does not have.
static void wifi_fan(struct canvas *c, int ax, int ay, int size, int lit, const struct statusbar_style *sty) {
    float unit = size / 4.f, thick = unit * 0.55f;
    for (int y = ay - size; y <= ay; y++)
        for (int x = ax - size; x <= ax + size; x++) {
            float dx = x - ax, dy = ay - y;
            if (dy < 0 || fabsf(dx) > dy) continue;      // 45 degrees each side
            float r = sqrtf(dx * dx + dy * dy);
            int ring = -1;
            if (r <= unit * 0.55f) ring = 0;
            else for (int k = 1; k <= 3; k++)
                if (r <= unit * (k + 0.5f) && r > unit * (k + 0.5f) - thick) ring = k;
            if (ring < 0) continue;
            canvas_put(c, x, y, ring <= lit ? sty->ink : sty->dim);
        }
}

// The battery: an outline with a nub, the charge as a fill, and a bolt
// while current flows in. (x, y) is the top-left of the body.
static void battery_icon(struct canvas *c, int x, int y, int w, int h, int cap, int charging, const struct statusbar_style *sty) {
    int r = h / 4, line = h / 9 > 1 ? h / 9 : 1, gap = line;
    int inner_r = r - line > 0 ? r - line : 1;
    canvas_round_rect(c, x, y, w, h, r, sty->ink);
    canvas_round_rect(c, x + line, y + line, w - 2 * line, h - 2 * line, inner_r, sty->hollow);
    canvas_fill_rect(c, x + w, y + h / 3, line + 1, h / 3, sty->ink);           // the nub
    int inner_w = w - 2 * (line + gap), inner_h = h - 2 * (line + gap);
    int fill_w = inner_w * (cap < 0 ? 0 : cap > 100 ? 100 : cap) / 100;
    uint32_t col = charging ? GREEN : cap <= 20 ? RED : sty->ink;
    if (fill_w > 0)
        canvas_round_rect(c, x + line + gap, y + line + gap, fill_w, inner_h, inner_r, col);
    if (charging) {
        int s = inner_h / 7 > 1 ? inner_h / 7 : 1;
        int bx = x + w / 2 - 5 * s / 2, by = y + h / 2 - 7 * s / 2;
        for (int row = 0; row < 7; row++)
            for (int k = 0; k < 5; k++)
                if (BOLT[row] & (16 >> k)) canvas_fill_rect(c, bx + k * s, by + row * s, s, s, sty->pale);
    }
}

// Everything is laid out in units of s, one 256th of the width: text 7
// units tall, the battery 13 by 7, the bar 12. Painted at four times that
// scale and averaged down, so the curves and the diagonals of the glyphs do
// not show the blocks they are built from.
#define UNIT(w) ((w) / 256)
#define SS 4

int statusbar_height(int w) { return 12 * UNIT(w); }

static void paint_at(struct canvas *c, const struct status *st, const struct statusbar_style *sty, int s) {
    int bar_h = 12 * s;
    // The panel's edge sits under the bezel by a few millimetres: measured
    // 2026-09-04, the battery's nub was cut off at a 3-unit margin.
    int margin = 12 * s;
    canvas_fill_rect(c, 0, 0, c->w, bar_h, sty->bar);
    int icon_h = 7 * s, icon_w = 13 * s;
    int y = (bar_h - icon_h) / 2;
    int right = c->w - margin - icon_w - s - 1;
    // iOS: plugged in is the bolt and the green, whatever the current does;
    // the rescue screen's text line still prints the milliamps.
    battery_icon(c, right, y, icon_w, icon_h, st->have_batt ? st->cap : 0, st->plugged, sty);
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", st->have_batt ? st->cap : 0);
    right -= 3 * s + canvas_text7_width(pct, s);
    canvas_text7(c, right, (bar_h - 7 * s) / 2, pct, s, sty->ink);
    right -= 5 * s + icon_h;
    wifi_fan(c, right, y + icon_h, icon_h, status_wifi_bars(st), sty);
}

// Average SS*SS supersampled pixels into one, alpha-weighted.
static void downsample(struct canvas *dst, const struct canvas *src) {
    for (int y = 0; y < dst->h; y++)
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
            uint32_t out = 0;
            if (a) out = ((a / (SS * SS)) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a);
            dst->px[y * dst->w + x] = out;
        }
}

void statusbar_paint(struct canvas *c, const struct status *st, const struct statusbar_style *sty) {
    struct canvas big = { calloc((size_t)c->w * SS * c->h * SS, 4), c->w * SS, c->h * SS };
    if (!big.px) return;
    paint_at(&big, st, sty, UNIT(c->w) * SS);
    downsample(c, &big);
    free(big.px);
}
