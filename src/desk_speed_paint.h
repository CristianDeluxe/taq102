// Draws the speed cards from the model, under the compact state row of the
// SPEED page. Reads the model only.
#ifndef DESK_SPEED_PAINT_H
#define DESK_SPEED_PAINT_H

#include "canvas.h"
#include "desk_paint.h"
#include "desk_speed.h"

void desk_speed_paint(struct canvas *c, const struct desk_speed *s, const struct desk_fonts *fonts);

#endif
