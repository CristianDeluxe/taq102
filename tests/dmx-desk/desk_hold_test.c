// SOURCES: desk_hold.c qlc_codec.c
// Concrete gesture sequences, including the exact wire frames and release order.
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "desk_hold.h"
#include "qlc_codec.h"

static void frame(struct hold_action a, int widget_id, int on, const char *expected) {
    assert(a.widget_id == widget_id && a.on == on);
    char wire[64];
    assert(qlc_encode_flash(wire, sizeof wire, a.widget_id, a.on) == (int)strlen(expected));
    assert(strcmp(wire, expected) == 0);
}

static void none(struct hold_action a) {
    assert(a.widget_id == -1 && a.on == 0);
}

int main(void) {
    struct desk_hold h;
    struct hold_action out[3];
    desk_hold_init(&h);
    assert(h.count == 0 && !h.link_ready);
    assert(desk_hold_add(&h, -1, HOLD_HIT, 3000, 0) == -1);
    assert(desk_hold_add(&h, 10, (enum hold_kind)2, 3000, 0) == -1);
    assert(desk_hold_add(&h, 10, HOLD_HIT, 0, 0) == -1);
    assert(desk_hold_add(&h, 10, HOLD_HIT, -1, 0) == -1);
    assert(desk_hold_add(&h, 10, HOLD_FOG, 1500, -1) == -1);
    assert(desk_hold_add(&h, 10, HOLD_HIT, 3000, 0) == 0);
    assert(desk_hold_add(&h, 10, HOLD_HIT, 3000, 0) == -1);
    assert(desk_hold_add(&h, 20, HOLD_HIT, 3000, 0) == 1);
    assert(desk_hold_add(&h, 30, HOLD_FOG, 1500, 10000) == 2);
    none(desk_hold_press(&h, 0, 4, 0)); // No link, no capture.
    assert(!h.control[0].down && !h.control[0].owed_release);
    desk_hold_set_link(&h, 1);
    none(desk_hold_press(&h, -1, 4, 0));
    none(desk_hold_press(&h, 3, 4, 0));
    none(desk_hold_press(&h, 0, -1, 0));
    none(desk_hold_press(&h, 0, 4, -1));
    none(desk_hold_release(&h, -1, 4, 0));
    none(desk_hold_release(&h, 3, 4, 0));
    assert(desk_hold_progress(&h, -1, 0) == -1);
    assert(desk_hold_progress(&h, 3, 0) == -1);
    assert(desk_hold_progress(&h, 0, 0) == -1);

    // One owner per control, with independent fingers on different controls.
    frame(desk_hold_press(&h, 0, 4, 0), 10, 1, "10|255");
    none(desk_hold_press(&h, 0, 4, 10));
    none(desk_hold_press(&h, 0, 5, 10));
    frame(desk_hold_press(&h, 1, 5, 10), 20, 1, "20|255");
    assert(desk_hold_owed(&h, out, 3) == 0); // Active holds are not pending offs.
    none(desk_hold_release(&h, 0, 5, 20));
    assert(h.control[0].down && h.control[0].slot == 4);
    frame(desk_hold_release(&h, 0, 4, 30), 10, 0, "10|0");
    none(desk_hold_release(&h, 0, 4, 31));
    frame(desk_hold_release(&h, 1, 5, 40), 20, 0, "20|0");
    assert(desk_hold_release_all(&h, 40, out, 3) == 0);

    // Hit cap fires at the boundary, remains captured, and lift sends no second 0.
    frame(desk_hold_press(&h, 0, 4, 100), 10, 1, "10|255");
    assert(desk_hold_progress(&h, 0, 99) == 0);
    assert(desk_hold_progress(&h, 0, 100) == 0);
    assert(desk_hold_progress(&h, 0, 1600) == 500);
    assert(desk_hold_tick(&h, 3099, out, 3) == 0);
    assert(desk_hold_progress(&h, 0, 3099) == 999);
    assert(desk_hold_tick(&h, 3100, out, 3) == 1);
    frame(out[0], 10, 0, "10|0");
    assert(h.control[0].down && h.control[0].capped);
    assert(desk_hold_progress(&h, 0, 9000) == 1000);
    assert(desk_hold_tick(&h, 9000, out, 3) == 0);
    none(desk_hold_press(&h, 0, 4, 9000));
    none(desk_hold_press(&h, 0, 6, 9000));
    none(desk_hold_release(&h, 0, 6, 9000));
    assert(h.control[0].capped);
    none(desk_hold_release(&h, 0, 4, 9000));
    assert(!h.control[0].down && !h.control[0].capped);
    assert(desk_hold_progress(&h, 0, 9000) == -1);
    frame(desk_hold_press(&h, 0, 6, 9000), 10, 1, "10|255");
    frame(desk_hold_release(&h, 0, 6, 9001), 10, 0, "10|0");

    // Fog cooldown starts at a normal release, and accepts exactly at expiry.
    frame(desk_hold_press(&h, 2, 7, 10000), 30, 1, "30|255");
    frame(desk_hold_release(&h, 2, 7, 10500), 30, 0, "30|0");
    none(desk_hold_press(&h, 2, 7, 20499));
    frame(desk_hold_press(&h, 2, 7, 20500), 30, 1, "30|255");
    assert(desk_hold_tick(&h, 21999, out, 3) == 0);
    assert(desk_hold_tick(&h, 22000, out, 3) == 1);
    frame(out[0], 30, 0, "30|0");
    assert(h.control[2].cooldown_until == 32000);
    none(desk_hold_release(&h, 2, 7, 23000));
    assert(h.control[2].cooldown_until == 32000); // Lift does not restart cooldown.
    none(desk_hold_press(&h, 2, 7, 31999));
    frame(desk_hold_press(&h, 2, 7, 32000), 30, 1, "30|255");
    assert(desk_hold_tick(&h, 33500, out, 3) == 1);
    frame(out[0], 30, 0, "30|0");
    none(desk_hold_press(&h, 2, 8, 43500)); // Still capped after cooldown expires.
    assert(desk_hold_release_all(&h, 43500, out, 3) == 0);

    // Lock/tab/settings/blank all use cancellation; control order is stable.
    for (int transition = 0; transition < 4; transition++) {
        int64_t t = 50000 + transition * 100;
        frame(desk_hold_press(&h, 1, 5, t), 20, 1, "20|255");
        frame(desk_hold_press(&h, 0, 4, t), 10, 1, "10|255");
        assert(desk_hold_release_all(&h, t + 1, out, 3) == 2);
        frame(out[0], 10, 0, "10|0");
        frame(out[1], 20, 0, "20|0");
        assert(!h.control[0].down && h.control[0].slot == -1);
        assert(desk_hold_release_all(&h, t + 2, out, 3) == 0);
        assert(desk_hold_tick(&h, t + 2, out, 3) == 0);
        none(desk_hold_release(&h, 0, 4, t + 2));
    }

    // Partial cancellation cancels every finger and queues the unreturned off.
    frame(desk_hold_press(&h, 0, 4, 60000), 10, 1, "10|255");
    frame(desk_hold_press(&h, 1, 5, 60000), 20, 1, "20|255");
    out[1] = (struct hold_action){ 999, 1 };
    assert(desk_hold_release_all(&h, 60001, out, 1) == 1);
    frame(out[0], 10, 0, "10|0");
    assert(out[1].widget_id == 999 && out[1].on == 1);
    assert(!h.control[1].down && h.control[1].owed_release);
    none(desk_hold_press(&h, 0, 4, 60002));
    assert(desk_hold_owed(&h, NULL, 3) == 0);
    assert(desk_hold_owed(&h, out, 0) == 0);
    assert(desk_hold_owed(&h, out, -1) == 0);
    assert(desk_hold_owed(&h, out, 1) == 1);
    frame(out[0], 20, 0, "20|0");
    assert(desk_hold_owed(&h, out, 3) == 0);

    // A cap with no space still caps all controls, then drains each off once.
    frame(desk_hold_press(&h, 0, 4, 61000), 10, 1, "10|255");
    frame(desk_hold_press(&h, 1, 5, 61000), 20, 1, "20|255");
    assert(desk_hold_tick(&h, 64000, NULL, 3) == 0);
    assert(h.control[0].capped && h.control[1].capped);
    assert(desk_hold_tick(&h, 64000, out, 1) == 1);
    frame(out[0], 10, 0, "10|0");
    none(desk_hold_release(&h, 0, 4, 64000));
    none(desk_hold_press(&h, 0, 4, 64000));
    assert(desk_hold_tick(&h, 64001, out, 1) == 1);
    frame(out[0], 20, 0, "20|0");
    assert(desk_hold_release_all(&h, 64002, out, 3) == 0);

    // Link loss retains obligations even if fingers lift/cancel while offline.
    frame(desk_hold_press(&h, 1, 5, 70000), 20, 1, "20|255");
    frame(desk_hold_press(&h, 0, 4, 70000), 10, 1, "10|255");
    desk_hold_set_link(&h, 0);
    desk_hold_set_link(&h, 0);
    none(desk_hold_press(&h, 2, 7, 70001));
    none(desk_hold_release(&h, 0, 4, 70001));
    assert(desk_hold_release_all(&h, 70001, out, 3) == 0);
    assert(desk_hold_tick(&h, 75000, out, 3) == 0);
    assert(desk_hold_owed(&h, out, 3) == 0);
    desk_hold_set_link(&h, 1);
    desk_hold_set_link(&h, 1);
    none(desk_hold_press(&h, 2, 7, 75000));
    assert(desk_hold_owed(&h, out, 1) == 1);
    frame(out[0], 10, 0, "10|0");
    none(desk_hold_press(&h, 0, 4, 75000));
    desk_hold_set_link(&h, 0); // Another disconnect preserves the remaining off.
    assert(desk_hold_owed(&h, out, 1) == 0);
    desk_hold_set_link(&h, 1);
    assert(desk_hold_owed(&h, out, 1) == 1);
    frame(out[0], 20, 0, "20|0");
    assert(desk_hold_owed(&h, out, 3) == 0);

    // Reconnect with the finger held never re-fires; later lift sends no off.
    frame(desk_hold_press(&h, 0, 4, 76000), 10, 1, "10|255");
    desk_hold_set_link(&h, 0);
    desk_hold_set_link(&h, 1);
    assert(desk_hold_owed(&h, out, 3) == 1);
    frame(out[0], 10, 0, "10|0");
    none(desk_hold_press(&h, 0, 4, 76001));
    desk_hold_set_link(&h, 0);
    desk_hold_set_link(&h, 1);
    assert(desk_hold_owed(&h, out, 3) == 0); // No duplicate after a second loss.
    none(desk_hold_release(&h, 0, 4, 76002));
    frame(desk_hold_press(&h, 0, 4, 76003), 10, 1, "10|255");
    assert(desk_hold_tick(&h, 79003, out, 3) == 1);
    frame(out[0], 10, 0, "10|0");
    desk_hold_set_link(&h, 0); // An already sent cap needs no reconnect off.
    desk_hold_set_link(&h, 1);
    assert(desk_hold_owed(&h, out, 3) == 0);
    assert(desk_hold_release_all(&h, 79004, out, 3) == 0);

    // Fog queued offline starts cooldown after the off is actually returned.
    frame(desk_hold_press(&h, 2, 7, 80000), 30, 1, "30|255");
    desk_hold_set_link(&h, 0);
    none(desk_hold_release(&h, 2, 7, 80500));
    desk_hold_set_link(&h, 1);
    none(desk_hold_press(&h, 2, 7, 100000));
    assert(desk_hold_owed(&h, out, 3) == 1);
    frame(out[0], 30, 0, "30|0");
    none(desk_hold_press(&h, 2, 7, 100000));
    none(desk_hold_press(&h, 2, 7, 109999));
    frame(desk_hold_press(&h, 2, 7, 110000), 30, 1, "30|255");
    assert(desk_hold_release_all(&h, 110500, out, 0) == 0);
    none(desk_hold_press(&h, 0, 4, 120500));
    assert(desk_hold_tick(&h, 130000, out, 3) == 1);
    frame(out[0], 30, 0, "30|0");
    none(desk_hold_press(&h, 2, 7, 139999));
    frame(desk_hold_press(&h, 2, 7, 140000), 30, 1, "30|255");
    frame(desk_hold_release(&h, 2, 7, 140001), 30, 0, "30|0");

    // Largest durations/timestamps do not overflow progress or cooldown math.
    desk_hold_init(&h);
    assert(desk_hold_add(&h, 10, HOLD_FOG, INT_MAX, INT_MAX) == 0);
    desk_hold_set_link(&h, 1);
    frame(desk_hold_press(&h, 0, 0, 0), 10, 1, "10|255");
    assert(desk_hold_progress(&h, 0, INT_MAX / 2) == 499);
    assert(desk_hold_progress(&h, 0, INT64_MAX) == 1000);
    assert(desk_hold_tick(&h, INT64_MAX - 1, out, 3) == 1);
    frame(out[0], 10, 0, "10|0");
    assert(h.control[0].cooldown_until == INT64_MAX);
    none(desk_hold_release(&h, 0, 0, INT64_MAX - 1));
    none(desk_hold_press(&h, 0, 0, INT64_MAX - 1));

    desk_hold_init(&h);
    for (int i = 0; i < 16; i++)
        assert(desk_hold_add(&h, i, HOLD_HIT, 3000, 0) == i);
    assert(desk_hold_add(&h, 16, HOLD_HIT, 3000, 0) == -1 && h.count == 16);
    puts("desk_hold ok");
    return 0;
}
