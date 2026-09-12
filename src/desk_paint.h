// Draws the model into a canvas: the placements of the view on screen, the
// headings of its bank, the rail with the page entries, the bank pills, the
// master column, and the link banner over everything when the master is gone.
#ifndef DESK_PAINT_H
#define DESK_PAINT_H

#include "canvas.h"
#include "desk_model.h"
#include "font.h"

struct desk_fonts {
    struct font *tile;    // Inter SemiBold 22, a cue's name
    struct font *value;   // Inter SemiBold 56, the master's number
    struct font *label;   // Inter Regular 20, the rail, headings and the link line
    struct font *small;   // Inter Regular 16, swatch and compact tiles
};

// Paints the whole screen. Damage tracking belongs to the presenter, which
// knows which buffer is stale; the painter always draws everything.
void desk_paint(struct canvas *canvas, const struct desk_model *model,
                const struct desk_fonts *fonts);

#endif
