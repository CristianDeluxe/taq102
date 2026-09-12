#include <stdio.h>
#include "canvas.h"
#include "font.h"
#include "rescue_paint.h"
#include "statusbar.h"

// Alpha 0xFF on purpose. The framebuffer is added as XRGB8888, but the stock
// 4.4.103 VOP driver blends it as ARGB: with 0x00 in the top byte the whole
// window is transparent and the panel shows a washed-out white with a ghost
// of the picture. Measured 2026-09-03; glcube never hit it because GBM
// buffers carry 0xFF. The own 4.4.167 kernel does not care either way.
#define AMBER 0xFFE08A00u
#define INK   0xFF201000u
#define PALE  0xFFFFF3D0u
#define DIM   0xFFB8741Cu   // unlit arcs and the status bar: amber, darkened

void rescue_paint(struct canvas *c, const char *kernel, const char *build,
                  const struct status *st, struct font *title, struct font *body,
                  const char *bar_font_path) {
    for (int i = 0; i < c->w * c->h; i++) c->px[i] = AMBER;
    const struct statusbar_style bar = { DIM, AMBER, INK, DIM, PALE, 0xFF34C759u, 0xFFFF3B30u, bar_font_path };
    statusbar_paint(c, st, &bar);
    int inter = title && body;
    if (inter) {
        const char *text = "Rescue mode";
        font_draw(title, c, (c->w - font_width(title, text)) / 2,
                  c->h / 5 + font_baseline(title), text, INK);
    } else {
        int big = c->w / 60;
        canvas_text(c, (c->w - canvas_text_width("RESCUE MODE", big)) / 2,
                    c->h / 5, "RESCUE MODE", big, INK);
    }
    char lines[5][160];
    snprintf(lines[0], sizeof lines[0], inter ? "Kernel %s" : "KERNEL %s", kernel);
    snprintf(lines[1], sizeof lines[1], inter ? "Build %s" : "BUILD %s", build);
    if (st->have_wifi)
        snprintf(lines[2], sizeof lines[2], inter ? "Wi-Fi %s %d dBm Q%d" : "WIFI %s %dDBM Q%d",
                 st->addr, st->level, st->quality);
    else snprintf(lines[2], sizeof lines[2], inter ? "Wi-Fi %s" : "WIFI %s", st->addr);
    if (st->have_batt)
        snprintf(lines[3], sizeof lines[3], inter ? "Battery %d%% %d.%02d V %s %d mA" : "BATTERY %d%% %d.%02dV %s %dMA",
                 st->cap, st->mv / 1000, (st->mv / 10) % 100, st->word, st->ma);
    else snprintf(lines[3], sizeof lines[3], inter ? "Battery unknown" : "BATTERY UNKNOWN");
    snprintf(lines[4], sizeof lines[4], "%s", inter ?
             "SSH root - ttyGS0 - killall rescue-screen to draw" :
             "SSH ROOT - TTYGS0 - KILLALL RESCUE-SCREEN TO DRAW");
    int s = c->w / 200;
    int x = c->w / 12, y = c->h / 2;
    for (int i = 0; i < 5; i++) {
        uint32_t col = i == 2 || i == 3 ? PALE : INK;
        if (inter) font_draw_fit(body, c, x, y + font_baseline(body), c->w - 2 * x, lines[i], col);
        else canvas_text(c, x, y, lines[i], s, col);
        y += inter ? 28 : 7 * s;
    }
}
