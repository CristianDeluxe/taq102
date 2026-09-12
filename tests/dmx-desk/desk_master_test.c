// SOURCES: desk_model.c desk_paint.c canvas.c canvas_blend.c font.c
// The master is unknown until the master speaks, a drag moves only what the
// desk asked for, and a finger on a tile never repaints what the show said.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canvas.h"
#include "desk_layout.h"
#include "desk_model.h"
#include "desk_paint.h"

static int count(const struct canvas *c, int x, int y, int w, int h, uint32_t col) {
    int n = 0;
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (c->px[j * c->w + i] == col)
                n++;
    return n;
}

int main(void) {
    struct desk_model m;
    desk_init(&m);
    struct desk_control master = { .kind = DESK_MASTER, .x = DESK_MASTER_TILE_X,
        .y = DESK_MASTER_TILE_Y, .w = DESK_MASTER_TILE_W, .h = DESK_MASTER_TILE_H,
        .enabled = 1, .widget_id = -1, .function_id = -1 };
    strcpy(master.label, "Master");
    struct desk_control cue = { .kind = DESK_CUE, .x = DESK_GRID_X, .y = DESK_GRID_Y,
        .w = DESK_TILE_W, .h = DESK_TILE_H, .enabled = 1, .widget_id = 4, .function_id = 720 };
    strcpy(cue.label, "AUTO");
    int im = desk_add(&m, &master);
    desk_add(&m, &cue);
    desk_set_link(&m, DESK_LINK_READY);
    assert(m.control[im].state == DESK_UNKNOWN);

    struct canvas c = { calloc(DESK_W * DESK_H, 4), DESK_W, DESK_H };
    struct desk_fonts fonts = { NULL, NULL, NULL };
    desk_paint(&c, &m, &fonts);
    // Unknown: not one amber pixel in the master tile.
    assert(count(&c, master.x, master.y, master.w, master.h, DESK_AMBER) == 0);

    // The first push, whatever its value, makes the level known.
    desk_apply_master(&m, 0);
    assert(m.control[im].state == DESK_ON && m.control[im].level == 0);
    desk_apply_master(&m, 128);
    assert(m.control[im].level == 128 && m.control[im].requested_level == 128);
    desk_paint(&c, &m, &fonts);
    int half = count(&c, master.x, master.y, master.w, master.h, DESK_AMBER);
    assert(half > master.w * master.h / 4 && half < master.w * master.h * 3 / 4);

    // A drag moves the requested level and the action, never the confirmed one.
    struct desk_action a = desk_touch_down(&m, 0, master.x + 10, master.y + master.h - 1);
    assert(a.kind == DESK_ACT_MASTER && a.value == 0);
    assert(m.control[im].requested_level == 0 && m.control[im].level == 128);
    a = desk_touch_move(&m, 0, master.x + 10, master.y);
    assert(a.kind == DESK_ACT_MASTER && a.value == 255 && m.control[im].level == 128);
    desk_paint(&c, &m, &fonts);
    int during = count(&c, master.x, master.y, master.w, master.h, DESK_AMBER);
    assert(during <= half && during > half * 9 / 10);    // the fill did not move
    // A push while the finger is down does not yank the request.
    desk_apply_master(&m, 140);
    assert(m.control[im].level == 140 && m.control[im].requested_level == 255);
    desk_touch_up(&m, 0, master.x + 10, master.y);
    // The master's own push, arriving after, is what moves it.
    desk_apply_master(&m, 255);
    assert(m.control[im].requested_level == 255);
    desk_paint(&c, &m, &fonts);
    assert(count(&c, master.x, master.y, master.w, master.h, DESK_AMBER) > half);

    // A pressed cue keeps its fill: a running cue stays amber under the
    // finger, and the outline is ink.
    desk_apply_function(&m, 720, 1);
    desk_paint(&c, &m, &fonts);
    int lit = count(&c, cue.x, cue.y, cue.w, cue.h, DESK_AMBER);
    assert(lit > cue.w * cue.h / 2);
    desk_touch_down(&m, 1, cue.x + 20, cue.y + 20);
    desk_paint(&c, &m, &fonts);
    int lit_pressed = count(&c, cue.x, cue.y, cue.w, cue.h, DESK_AMBER);
    assert(lit_pressed > lit * 8 / 10);
    assert(count(&c, cue.x, cue.y, cue.w, cue.h, DESK_INK) > 2 * (cue.w + cue.h));
    free(c.px);
    printf("desk_master ok\n");
    return 0;
}
