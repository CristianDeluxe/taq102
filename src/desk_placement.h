// A control's place on one bank of one page. Geometry lives here and nowhere
// else: a control is a fact about the show, a placement a fact about the
// screen, and the same control (AUTO, say) has a placement on every page.
#ifndef DESK_PLACEMENT_H
#define DESK_PLACEMENT_H

enum desk_tile {
    TILE_CUE,       // 218x96, caption on two lines, a detail line when known
    TILE_WIDE,      // 452x96, the first state of the room
    TILE_SWATCH,    // 100x88, a swatch ring over a small caption
    TILE_COMPACT,   // 86x56, the room's states on every other page
    TILE_HAZE,      // 160x96, four to a row
    TILE_MASTER,    // the fader in the right column
    TILE_PANIC,     // the stop-all under it
};

struct desk_placement {
    int control;    // index into the model's controls
    int page, bank;
    int x, y, w, h;
    enum desk_tile tile;
};

#endif
