#ifndef CANVAS_BLEND_H
#define CANVAS_BLEND_H

#include <stdint.h>
#include "canvas.h"

uint32_t canvas_over(uint32_t dst, uint32_t src);
void canvas_blend(struct canvas *c, int x, int y, uint32_t col);
void canvas_blend_rect(struct canvas *c, int x, int y, int w, int h, uint32_t col);
void canvas_blend_round_rect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t col);

#endif
