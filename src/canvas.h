// A software canvas of 0xAARRGGBB pixels with the few primitives the status
// bar and the rescue screen draw with: pixels, rectangles, rounded
// rectangles and a 3x5 glyph font. Nothing here touches the display.
#ifndef CANVAS_H
#define CANVAS_H
#include <stdint.h>

// A clip of zero width means none: the initialisers `{ px, w, h }` all over
// the tree keep painting the whole canvas. A painter that repaints damage
// sets one, and every primitive here and in canvas_blend honours it.
struct canvas { uint32_t *px; int w, h; int clip_x, clip_y, clip_w, clip_h; };

void canvas_set_clip(struct canvas *c, int x, int y, int w, int h);
void canvas_clear_clip(struct canvas *c);
// Whether a pixel is on the canvas and inside its clip.
static inline int canvas_visible(const struct canvas *c, int x, int y) {
    if (x < 0 || y < 0 || x >= c->w || y >= c->h)
        return 0;
    if (c->clip_w <= 0)
        return 1;
    return x >= c->clip_x && x < c->clip_x + c->clip_w && y >= c->clip_y && y < c->clip_y + c->clip_h;
}

void canvas_put(struct canvas *c, int x, int y, uint32_t col);
void canvas_fill_rect(struct canvas *c, int x, int y, int w, int h, uint32_t col);
// A rectangle with rounded corners of radius r, filled.
void canvas_round_rect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t col);
// A..Z (either case), 0..9 and . : - / % at s pixels per glyph cell.
void canvas_text(struct canvas *c, int x, int y, const char *str, int s, uint32_t col);
int canvas_text_width(const char *str, int s);
// Digits and '%' in a 5x7 face, for the status bar's percentage.
void canvas_text7(struct canvas *c, int x, int y, const char *str, int s, uint32_t col);
int canvas_text7_width(const char *str, int s);

#endif
