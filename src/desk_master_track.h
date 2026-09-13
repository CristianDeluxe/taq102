#ifndef DESK_MASTER_TRACK_H
#define DESK_MASTER_TRACK_H

#include "desk_placement.h"
#include "desk_view.h"
#include "font.h"

// Resolve with the same value font used to paint, including its glyph fallback.
// Input passes this rectangle to the model; the model never loads a font.
struct desk_rect desk_master_track(const struct desk_placement *p, struct font *value);

#endif
