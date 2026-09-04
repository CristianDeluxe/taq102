// A software canvas of 0xAARRGGBB pixels with the few primitives the status
// bar and the rescue screen draw with: pixels, rectangles, rounded
// rectangles and a 3x5 glyph font. Nothing here touches the display.
#ifndef CANVAS_H
#define CANVAS_H
#include <stdint.h>

struct canvas { uint32_t *px; int w, h; };

void canvas_put(struct canvas *c, int x, int y, uint32_t col);
void canvas_fill_rect(struct canvas *c, int x, int y, int w, int h, uint32_t col);
// A rectangle with rounded corners of radius r, filled.
void canvas_round_rect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t col);
// A..Z (either case), 0..9 and . : - / % at s pixels per glyph cell.
void canvas_text(struct canvas *c, int x, int y, const char *str, int s, uint32_t col);
int canvas_text_width(const char *str, int s);

#endif
