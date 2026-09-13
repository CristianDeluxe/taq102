// Draws the speed cards from the model, under the compact state row of the
// SPEED page. Reads the model only.
#ifndef DESK_SPEED_PAINT_H
#define DESK_SPEED_PAINT_H

#include "canvas.h"
#include "desk_paint.h"
#include "desk_speed.h"

void desk_speed_paint(struct canvas *c, const struct desk_speed *s, const struct desk_fonts *fonts);
// SHOW's tempo card for dial `i` inside (x, y, w, h): the base BPM large and
// a TAP target at the right; `pressed` outlines the target.
void desk_speed_paint_tempo(struct canvas *c, const struct desk_speed *s, int i,
                            const struct desk_fonts *fonts, int x, int y, int w, int h, int pressed);
// The TAP target's rectangle inside the same card, for the hit test.
void desk_speed_tempo_tap_rect(int x, int y, int w, int h, int *tx, int *ty, int *tw, int *th);

#endif
