// Turns the map's pages and sections into placements, bank by bank. Pure
// arithmetic over the fixed chrome: sections flow top to bottom with a
// heading each, a section that does not fit the room left moves to the next
// bank, and a section taller than a bank is split with its heading repeated
// as "2/3". The room's state section is placed again, compact, at the top of
// every other page, and the master and the panic button on every bank.
#ifndef DESK_LAYOUT_RESOLVE_H
#define DESK_LAYOUT_RESOLVE_H

#include "desk_placement.h"
#include "showmap.h"

#define DESK_MAX_PLACEMENTS 320
#define DESK_MAX_BANKS 6
#define DESK_MAX_HEADINGS 64

struct desk_heading {
    int page, bank;
    int x, y, w;
    char text[MAP_CAPTION_MAX];
    int part, parts;    // 1/1 when the section was not split
};

struct desk_layout {
    struct desk_placement placement[DESK_MAX_PLACEMENTS];
    int placements;
    struct desk_heading heading[DESK_MAX_HEADINGS];
    int headings;
    int banks[MAP_MAX_PAGES];
    char title[MAP_MAX_PAGES][MAP_CAPTION_MAX];
    int pages;
    int speed_page;     // the SPEED page's index after the map's pages, or -1
};

// `master` and `panic` are the model indices of the two chrome controls.
// A map with dials gets one more page, `SPEED`, carrying only the compact
// state row and the chrome; the speed cards under it are another model's.
// Returns 0, or -1 when a page needs more banks or placements than the
// layout holds, which no map this size can cause.
int desk_layout_resolve(const struct show_map *map, int master, int panic,
                        struct desk_layout *out);

#endif
