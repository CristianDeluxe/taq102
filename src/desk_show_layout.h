// SHOW: the page for the night, a fixed composition rather than a flow.
// Row one the room's states, row two the hits as holds with the one toggle
// beside them, row three the fog holds and the ambient selector, row four
// the rig's colours and the tempo card. Every control comes from the map's
// LIVE page and COLOR's single-colour picks; the OFF segment and the tempo
// card are the model's own pseudo controls, given by index.
#ifndef DESK_SHOW_LAYOUT_H
#define DESK_SHOW_LAYOUT_H

#include "desk_layout_resolve.h"
#include "showmap.h"

// Composes page `page` of `out` from the map's LIVE page (its states, haze
// rhythms and accents) and COLOR's rig colours. `haze_off` and `tempo` are
// model indices of the pseudo controls, -1 when absent. Returns 0, or -1
// when the layout has no room.
int desk_show_compose(const struct show_map *map, int page, int haze_off, int tempo,
                      struct desk_layout *out);

// The map's LIVE page index, or -1 when the map has none: SHOW replaces it.
int desk_show_live_page(const struct show_map *map);

#endif
