// Draws the settings surface over the desk: the two cards, the footer, the
// confirmation sheet and the keyboard. Reads the model only.
#ifndef DESK_SETUP_PAINT_H
#define DESK_SETUP_PAINT_H

#include "canvas.h"
#include "desk_paint.h"
#include "desk_setup.h"

void desk_setup_paint(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts);

#endif
