// A control's place on one bank of one page. Geometry lives here and nowhere
// else: a control is a fact about the show, a placement a fact about the
// screen, and the same control (AUTO, say) has a placement on every page.
#ifndef DESK_PLACEMENT_H
#define DESK_PLACEMENT_H

enum desk_tile {
    TILE_CUE,       // 200x64, a state, a hook, a toggle, a chase
    TILE_WIDE,      // 200x64 as well; kept for the hand-laid tests
    TILE_SWATCH,    // 128x88, a pick: its colour as a disc over its name
    TILE_COMPACT,   // 128x64, a small button
    TILE_HAZE,      // 200x64, an ambient rhythm
    TILE_HOLD,      // 128x104, a hit: held on the Mac, or fired while held here
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
