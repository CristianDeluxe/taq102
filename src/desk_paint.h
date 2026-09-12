// Draws the desk into a canvas, in the appliance's own palette and type.
// Reads the model, never changes it.
#ifndef DESK_PAINT_H
#define DESK_PAINT_H

#include "canvas.h"
#include "desk_model.h"
#include "font.h"

struct desk_fonts {
    struct font *tile;    // Inter SemiBold 22, a cue's name
    struct font *value;   // Inter SemiBold 56, the master's number
    struct font *label;   // Inter Regular 20, the rail and the link line
};

// Paints the whole screen. Damage tracking belongs to the presenter, which
// knows which buffer it is filling.
void desk_paint(struct canvas *canvas, const struct desk_model *model,
                const struct desk_fonts *fonts);

#endif
