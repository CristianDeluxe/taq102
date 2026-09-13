// SOURCES: desk_build_holds.c desk_hold.c desk_model.c desk_master_level_at.c
// Every enabled hit needs a slot; an over-capacity model must say unavailable.
#include <assert.h>
#include <stdio.h>

#include "desk_build_holds.h"
#include "desk_burst_base.h"
#include "touch_input.h"

int main(void) {
    struct desk_model m;
    struct desk_hold h;
    int owner[TOUCH_MAX_SLOTS];
    desk_init(&m);
    for (int i = 0; i < DESK_MAX_CONTROLS; i++) {
        struct desk_control c = {
            .kind = DESK_BURST, .function_id = i, .enabled = 1, .burst_ms = 3000
        };
        assert(desk_add(&m, &c) == i);
    }
    desk_build_holds(&h, &m, owner);
    assert(h.count == MAP_MAX_CONTROLS);
    for (int i = 0; i < m.count; i++) {
        const struct desk_control *c = &m.control[i];
        assert(!c->enabled || c->hold_index >= 0);
        if (i < MAP_MAX_CONTROLS) {
            assert(c->enabled && c->hold_index == i);
            assert(h.control[i].slot == -1);
        } else {
            assert(!c->enabled && c->hold_index == -1 && c->reason[0]);
        }
    }
    desk_hold_set_link(&h, 1);
    // The shipped map's seventeenth burst and the capacity boundary both fire.
    assert(desk_hold_press(&h, 16, 0, 0).widget_id == BURST_BASE + 16);
    assert(desk_hold_release(&h, 16, 0, 1).widget_id == BURST_BASE + 16);
    assert(desk_hold_press(&h, MAP_MAX_CONTROLS - 1, 0, 2).on == 1);
    assert(desk_hold_release(&h, MAP_MAX_CONTROLS - 1, 0, 3).on == 0);
    for (int s = 0; s < TOUCH_MAX_SLOTS; s++)
        assert(owner[s] == -1);

    // A mixed map shares the same capacity; bad settings cannot leave live tiles.
    desk_init(&m);
    struct desk_control c = { .kind = DESK_HOLD, .widget_id = 1, .enabled = 1 };
    assert(desk_add(&m, &c) == 0);
    c.kind = DESK_BURST; c.function_id = 1; c.burst_ms = 3000;
    assert(desk_add(&m, &c) == 1);
    c.function_id = 2; c.burst_ms = 0;
    assert(desk_add(&m, &c) == 2);
    desk_build_holds(&h, &m, owner);
    assert(h.count == 2 && m.control[0].enabled && m.control[1].enabled);
    assert(!m.control[2].enabled && m.control[2].reason[0]);
    puts("desk_build_holds ok");
    return 0;
}
