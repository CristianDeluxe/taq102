// The pager segment's hit rectangle, which is taller than the one it is drawn
// in: it runs from the strip down to the bottom of the glass, so a finger
// aimed at a bar sitting against the bezel is not lost in the dead margin
// under it. Painting still uses desk_view_pager.
#ifndef DESK_PAGER_HIT_H
#define DESK_PAGER_HIT_H

#include "desk_layout_resolve.h"
#include "desk_view.h"

struct desk_rect desk_pager_hit(int index, const struct desk_layout *layout, int page);

#endif
