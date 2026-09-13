#include "desk_pager_hit.h"

#include "desk_layout.h"

// The pager is drawn 56 px tall and then the glass ends 16 px later, so the
// strip is the one control with dead margin beneath it and a bezel beyond
// that. Measured on the tablet 2026-09-13, with I pressing the bar as
// he would at a gig: presses aimed at its lower edge landed at y 587..596,
// past the drawn bottom of 583, and the desk saw nothing at all -- the tap
// never became a bank tap, so the release margin could not rescue it either.
// Aiming low at a target that sits against the bezel is what a finger does;
// the margin below belongs to nobody, so it belongs to the pager.
struct desk_rect desk_pager_hit(int index, const struct desk_layout *layout, int page) {
    struct desk_rect r = desk_view_pager(index, layout, page);
    if (r.w <= 0 || r.h <= 0)
        return r;
    r.h = DESK_H - r.y;
    return r;
}
