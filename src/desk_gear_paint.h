// The gear at the left of the status bar that opens the settings surface:
// a ring with eight teeth, drawn from rectangles so it needs no glyph.
#ifndef DESK_GEAR_PAINT_H
#define DESK_GEAR_PAINT_H

#include <stdint.h>

#include "canvas.h"

void desk_gear_paint(struct canvas *c, int x, int y, int w, int h, uint32_t ink, uint32_t hole);

#endif
