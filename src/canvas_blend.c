#include <stdint.h>
#include <stddef.h>
#include "canvas_blend.h"

uint32_t canvas_over(uint32_t dst, uint32_t src) {
    unsigned sa = src >> 24;
    if (!sa) return dst;
    if (sa == 255) return src;
    // Keep the destination weight unrounded until the final channel division.
    unsigned ws = sa * 255, wd = (dst >> 24) * (255 - sa), sum = ws + wd;
    unsigned a = (sum + 127) / 255;
    unsigned r = (((src >> 16) & 255) * ws + ((dst >> 16) & 255) * wd + sum / 2) / sum;
    unsigned g = (((src >> 8) & 255) * ws + ((dst >> 8) & 255) * wd + sum / 2) / sum;
    unsigned b = ((src & 255) * ws + (dst & 255) * wd + sum / 2) / sum;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void canvas_blend(struct canvas *c, int x, int y, uint32_t col) {
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    uint32_t *p = &c->px[(size_t)y * c->w + x];
    *p = canvas_over(*p, col);
}

void canvas_blend_rect(struct canvas *c, int x, int y, int w, int h, uint32_t col) {
    if (w <= 0 || h <= 0) return;
    int64_t right = (int64_t)x + w, bottom = (int64_t)y + h;
    if (right > c->w) right = c->w;
    if (bottom > c->h) bottom = c->h;
    for (int j = y < 0 ? 0 : y; j < bottom; j++)
        for (int i = x < 0 ? 0 : x; i < right; i++) canvas_blend(c, i, j, col);
}

void canvas_blend_round_rect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t col) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int64_t right = (int64_t)x + w, bottom = (int64_t)y + h;
    if (right > c->w) right = c->w;
    if (bottom > c->h) bottom = c->h;
    for (int j = y < 0 ? 0 : y; j < bottom; j++) {
        for (int i = x < 0 ? 0 : x; i < right; i++) {
            int inside = 0;
            // Subpixel coverage scales alpha while keeping straight RGB intact.
            for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
                double px = (double)i - x + (sx + 0.5) / 4;
                double py = (double)j - y + (sy + 0.5) / 4;
                double cx = px < r ? r : px > w - r ? w - r : px;
                double cy = py < r ? r : py > h - r ? h - r : py;
                double dx = px - cx, dy = py - cy;
                if (dx * dx + dy * dy <= (double)r * r) inside++;
            }
            unsigned a = (col >> 24) * inside / 16;
            canvas_blend(c, i, j, (a << 24) | (col & 0x00FFFFFFu));
        }
    }
}
