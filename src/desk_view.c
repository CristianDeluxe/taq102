#include "desk_view.h"

#include "desk_layout.h"

struct desk_rect desk_view_rail_entry(int index) {
    struct desk_rect r = { 0, DESK_RAIL_FIRST_Y + index * DESK_RAIL_ENTRY_H, DESK_RAIL_W,
                           DESK_RAIL_ENTRY_H };
    return r;
}

struct desk_rect desk_view_lock_target(void) {
    // Centred on the rail, sitting above the link word at the bottom.
    struct desk_rect r = { (DESK_RAIL_W - DESK_LOCK_SIZE) / 2,
                           DESK_H - 56 - 16 - DESK_LOCK_SIZE, DESK_LOCK_SIZE, DESK_LOCK_SIZE };
    return r;
}

struct desk_rect desk_view_bank_button(int index) {
    struct desk_rect r = { DESK_GRID_X + index * (DESK_BANK_PILL_W + DESK_GAP),
                           DESK_H - DESK_SELECTOR_H + 4, DESK_BANK_PILL_W, DESK_SELECTOR_H - 8 };
    return r;
}

int desk_rect_contains(struct desk_rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
