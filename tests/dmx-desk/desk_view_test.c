// SOURCES: desk_view.c desk_input.c desk_model.c desk_layout_resolve.c desk_show_layout.c showmap.c
// The rail, the bank pills and the lock target: where they are, and which one
// a finger lands on. A tap completes only on the entry it started on.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_input.h"
#include "desk_layout.h"
#include "desk_layout_resolve.h"
#include "desk_model.h"
#include "desk_view.h"
#include "showmap.h"

static struct touch_event ev(enum touch_kind kind, int slot, int x, int y) {
    struct touch_event e = { kind, slot, (float)x, (float)y, 0 };
    return e;
}

int main(void) {
    struct desk_rect e0 = desk_view_tab(0), e1 = desk_view_tab(1);
    assert(e0.x == 16 && e0.y == 0 && e0.w == 76 && e0.h == 48 && e0.y + e0.h <= DESK_BAR_H);
    assert(e1.x == e0.x + 80);
    struct desk_rect lock = desk_view_lock_target(), gear = desk_view_gear();
    assert(lock.w == 48 && lock.h == 48 && lock.x + lock.w <= DESK_W && lock.y == 0);
    assert(gear.x + gear.w <= lock.x);
    struct desk_rect b1 = desk_view_pager(1, 2);
    assert(b1.x > DESK_CONTENT_X && b1.y >= DESK_PAGER_Y && b1.x + b1.w <= DESK_CONTENT_X + DESK_CONTENT_W);

    struct show_map map;
    assert(showmap_load("show/vibra.desk.json", &map) == 0);
    struct desk_model m;
    desk_init(&m);
    struct desk_layout layout;
    assert(desk_layout_resolve(&map, map.count, map.count + 1, map.count + 2, map.count + 3, &layout) == 0);
    desk_set_layout(&m, &layout);

    int index;
    assert(desk_input_target(&m, 10, 2, &index) == TARGET_NONE);            // the bar's margin
    assert(desk_input_target(&m, e1.x + 10, e1.y + 10, &index) == TARGET_RAIL && index == 1);
    assert(desk_input_target(&m, desk_view_tab(9).x + 10, 20, &index) != TARGET_RAIL);  // no tenth page
    assert(desk_input_target(&m, lock.x + 5, lock.y + 5, &index) == TARGET_LOCK);
    assert(desk_input_target(&m, gear.x + 5, gear.y + 5, &index) == TARGET_NONE);  // the gear is the caller's
    assert(desk_input_target(&m, 400, 300, &index) == TARGET_CONTENT);
    // A page with more than one bank has a pager at the bottom right.
    int paged = -1;
    for (int i = 0; i < layout.pages; i++)
        if (layout.banks[i] > 1) { paged = i; break; }
    assert(paged >= 0);
    desk_set_view(&m, paged, 0);
    struct desk_rect p1 = desk_view_pager(1, layout.banks[paged]);
    assert(desk_input_target(&m, p1.x + 5, p1.y + 5, &index) == TARGET_BANK && index == 1);
    // A page with one bank has no pager: the same point is content.
    desk_set_view(&m, 6, 0);
    assert(layout.banks[6] == 1);
    assert(desk_input_target(&m, p1.x + 5, p1.y + 5, &index) == TARGET_CONTENT);
    desk_set_view(&m, 0, 0);

    struct desk_input in;
    desk_input_init(&in);
    // Down on LIVE, up on COLOR: nothing.
    struct touch_event d = ev(TOUCH_DOWN, 0, e0.x + 20, e0.y + 10);
    assert(desk_input_feed(&in, &m, &d, &index) == TARGET_RAIL && index == -1);
    struct touch_event u = ev(TOUCH_UP, 0, e1.x + 20, e1.y + 10);
    assert(desk_input_feed(&in, &m, &u, &index) == TARGET_RAIL && index == -1);
    // Down and up on COLOR: page 1.
    d = ev(TOUCH_DOWN, 0, e1.x + 20, e1.y + 10);
    desk_input_feed(&in, &m, &d, &index);
    u = ev(TOUCH_UP, 0, e1.x + 20, e1.y + 10);
    assert(desk_input_feed(&in, &m, &u, &index) == TARGET_RAIL && index == 1);
    // A content contact stays content through its moves.
    d = ev(TOUCH_DOWN, 1, 400, 300);
    assert(desk_input_feed(&in, &m, &d, &index) == TARGET_CONTENT);
    struct touch_event mv = ev(TOUCH_MOVE, 1, 20, e1.y + 10);
    assert(desk_input_feed(&in, &m, &mv, &index) == TARGET_CONTENT);
    u = ev(TOUCH_UP, 1, 20, e1.y + 10);
    assert(desk_input_feed(&in, &m, &u, &index) == TARGET_CONTENT && index == -1);
    printf("desk_view ok\n");
    return 0;
}
