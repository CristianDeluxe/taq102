#include <stdio.h>
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

int statusbar_height(int w) { return 11 * (w / 200); }

void statusbar_paint(struct canvas *c, const struct status *st, const struct statusbar_style *sty) {
    int s = c->w / 200;                     // the text scale used below
    int bar_h = statusbar_height(c->w);
    int margin = 3 * s;
    canvas_fill_rect(c, 0, 0, c->w, bar_h, sty->bar);
    int icon_h = 5 * s, icon_w = 11 * s;
    int y = (bar_h - icon_h) / 2;
    int right = c->w - margin - icon_w - s - 1;
    battery_icon(c, right, y, icon_w, icon_h, st->have_batt ? st->cap : 0, st->have_batt && st->ma > 0, sty);
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", st->have_batt ? st->cap : 0);
    right -= 2 * s + canvas_text_width(pct, s);
    canvas_text(c, right, (bar_h - 5 * s) / 2, pct, s, sty->ink);
    right -= 4 * s + icon_h;
    wifi_fan(c, right, y + icon_h, icon_h + s, status_wifi_bars(st), sty);
}
