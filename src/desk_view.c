#include "desk_view.h"

#include "desk_layout.h"

struct desk_rect desk_view_tab(int index) {
    struct desk_rect r = { DESK_TAB_X + index * (DESK_TAB_W + DESK_TAB_GAP), DESK_TAB_Y,
                           DESK_TAB_W, DESK_TAB_H };
    return r;
}

struct desk_rect desk_view_gear(void) {
    struct desk_rect r = { DESK_GEAR_X, 0, DESK_TARGET, DESK_TARGET };
    return r;
}

struct desk_rect desk_view_lock_target(void) {
    struct desk_rect r = { DESK_LOCK_X, 0, DESK_TARGET, DESK_TARGET };
    return r;
}

struct desk_rect desk_view_pager(int index, int banks) {
    int right = DESK_CONTENT_X + DESK_CONTENT_W;
    struct desk_rect r = { right - (banks - index) * (DESK_PAGER_W + DESK_GAP) + DESK_GAP,
                           DESK_PAGER_Y, DESK_PAGER_W, DESK_PAGER_H };
    return r;
}

int desk_rect_contains(struct desk_rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
