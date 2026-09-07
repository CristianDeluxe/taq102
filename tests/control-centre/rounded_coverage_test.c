// SOURCES: canvas_blend.c
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "canvas_blend.h"

/* Original 4x4 renderer is the oracle, including clipping and translucent RGB. */
static void reference(struct canvas *c, int x, int y, int w, int h, int r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int j = 0; j < c->h; j++) for (int i = 0; i < c->w; i++) {
        if (i < x || j < y || i >= (int64_t)x + w || j >= (int64_t)y + h) continue;
        int covered = 0;
        for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
            double px = (double)i - x + (sx + .5) / 4;
            double py = (double)j - y + (sy + .5) / 4;
            double cx = px < r ? r : px > w - r ? w - r : px;
            double cy = py < r ? r : py > h - r ? h - r : py;
            double dx = px - cx, dy = py - cy;
            covered += dx * dx + dy * dy <= (double)r * r;
        }
        canvas_blend(c, i, j, ((color >> 24) * covered / 16 << 24) | (color & 0xffffff));
    }
}
int main(void) {
    uint32_t a[32*24], b[32*24];
    struct canvas actual = {a, 32, 24}, expected = {b, 32, 24};
    uint32_t seed = 42;
    const uint32_t colors[] = {0, 0x00123456, 0xc7141416, 0xeb2a2a2e, 0xffe08a00, 0x0188ff00};
    for (int test = 0; test < 300; test++) {
        for (int i = 0; i < 32*24; i++) {
            seed = seed * 1664525u + 1013904223u;
            a[i] = b[i] = test % 2 ? seed : 0xc7141416;
        }
        int x = test % 47 - 15, y = test % 37 - 12;
        int w = test % 151 - 2, h = (test * 7) % 153 - 2, r = test % 100 - 3;
        uint32_t color = colors[test % 6];
        canvas_blend_round_rect(&actual, x, y, w, h, r, color);
        reference(&expected, x, y, w, h, r, color);
        assert(memcmp(a, b, sizeof a) == 0);
    }
    canvas_blend_round_rect(&actual, INT_MAX - 2, INT_MAX - 2, 50, 50, 20, 0xffffffff);
    assert(memcmp(a, b, sizeof a) == 0);
    puts("rounded coverage: 300 reference comparisons ok");
    return 0;
}
