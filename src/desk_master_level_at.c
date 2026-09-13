#include "desk_master_level_at.h"

#include "desk_master_metrics.h"

// The 24 px grip moves between centres at y + 12 and y + h - 12.
// Round the inverse of that travel to the nearest DMX level, then let the
// endpoint bands saturate without rescaling the interior under the finger.
int desk_master_level_at(const struct desk_rect *track, int y) {
    int top = track->y + DESK_MASTER_THUMB_H / 2;
    int span = track->h - DESK_MASTER_THUMB_H;
    int bottom = top + span;
    if (span <= 2 * DESK_MASTER_END_MARGIN)
        return y <= track->y + track->h / 2 ? 255 : 0;
    if (y <= top + DESK_MASTER_END_MARGIN)
        return 255;
    if (y >= bottom - DESK_MASTER_END_MARGIN)
        return 0;
    return ((bottom - y) * 255 + span / 2) / span;
}
