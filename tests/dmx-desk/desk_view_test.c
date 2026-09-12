// SOURCES: desk_view.c desk_input.c desk_model.c desk_layout_resolve.c showmap.c
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
    struct desk_rect e0 = desk_view_rail_entry(0), e1 = desk_view_rail_entry(1);
    assert(e0.x == 0 && e0.y == DESK_BAR_H + 16 && e0.w == DESK_RAIL_W && e0.h == 44);
    assert(e1.y == e0.y + 44);
    struct desk_rect lock = desk_view_lock_target();
    assert(lock.w == 72 && lock.x + lock.w <= DESK_RAIL_W && lock.y + lock.h < DESK_H - 46);
    struct desk_rect b1 = desk_view_bank_button(1);
    assert(b1.x > DESK_GRID_X && b1.y >= DESK_H - 56 && b1.x + b1.w < DESK_MASTER_X);

    struct show_map map;
    assert(showmap_load("show/vibra.desk.json", &map) == 0);
    struct desk_model m;
    desk_init(&m);
    struct desk_layout layout;
    assert(desk_layout_resolve(&map, map.count, map.count + 1, &layout) == 0);
    desk_set_layout(&m, &layout);

    int index;
    assert(desk_input_target(&m, 10, 10, &index) == TARGET_NONE);           // the bar
    assert(desk_input_target(&m, 20, e1.y + 10, &index) == TARGET_RAIL && index == 1);
    assert(desk_input_target(&m, 20, e0.y + 8 * 44, &index) != TARGET_RAIL);  // no eighth page
    assert(desk_input_target(&m, lock.x + 5, lock.y + 5, &index) == TARGET_LOCK);
    assert(desk_input_target(&m, 400, 300, &index) == TARGET_CONTENT);
    // LIVE has more than one bank, so the strip at the bottom is pills.
    assert(layout.banks[0] > 1);
    assert(desk_input_target(&m, b1.x + 5, b1.y + 5, &index) == TARGET_BANK && index == 1);
    // A page with one bank has no strip: the same point is content.
    desk_set_view(&m, 6, 0);
    assert(layout.banks[6] == 1);
    assert(desk_input_target(&m, b1.x + 5, b1.y + 5, &index) == TARGET_CONTENT);
    desk_set_view(&m, 0, 0);

    struct desk_input in;
    desk_input_init(&in);
    // Down on LIVE, up on COLOR: nothing.
    struct touch_event d = ev(TOUCH_DOWN, 0, 20, e0.y + 10);
    assert(desk_input_feed(&in, &m, &d, &index) == TARGET_RAIL && index == -1);
    struct touch_event u = ev(TOUCH_UP, 0, 20, e1.y + 10);
    assert(desk_input_feed(&in, &m, &u, &index) == TARGET_RAIL && index == -1);
    // Down and up on COLOR: page 1.
    d = ev(TOUCH_DOWN, 0, 20, e1.y + 10);
    desk_input_feed(&in, &m, &d, &index);
    u = ev(TOUCH_UP, 0, 20, e1.y + 10);
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
