// SOURCES: desk_model.c desk_master_level_at.c desk_master_track.c desk_paint.c desk_pager_caption.c desk_pager_bank_caption.c icon.c desk_caption.c desk_view.c desk_view_pager.c desk_pager_label.c desk_layout_resolve.c desk_show_layout.c canvas.c canvas_blend.c font.c desk_fonts.c
// The production placement and loaded font must agree with both touch and paint.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "canvas.h"
#include "desk_fonts.h"
#include "desk_layout.h"
#include "desk_layout_resolve.h"
#include "desk_master_metrics.h"
#include "desk_master_track.h"
#include "desk_model.h"
#include "desk_paint.h"

int main(void) {
    struct desk_fonts loaded = {0};
    assert(desk_fonts_open(&loaded, "br2-external/package/taq102-fonts/fonts") == 0);
    assert(loaded.value);
    struct canvas c = { .px = calloc(DESK_W * DESK_H, sizeof(uint32_t)),
                        .w = DESK_W, .h = DESK_H };
    assert(c.px);
    for (int fallback = 0; fallback <= 1; fallback++) {
        struct desk_fonts empty = {0};
        const struct desk_fonts *fonts = fallback ? &empty : &loaded;
        struct desk_model m;
        desk_init(&m);
        struct desk_control master = { .kind = DESK_MASTER, .enabled = 1, .widget_id = 42 };
        int im = desk_add(&m, &master);
        struct desk_control panic = { .kind = DESK_STOP_ALL, .enabled = 1 };
        int ip = desk_add(&m, &panic);
        struct show_map map = { .pages = 1 };
        struct desk_layout layout;
        assert(desk_layout_resolve(&map, im, ip, -1, -1, &layout) == 0);
        desk_set_layout(&m, &layout);
        desk_set_link(&m, DESK_LINK_READY);
        const struct desk_placement *p = desk_placement_of(&m, im);
        assert(p && p->y == 56 && p->h == 372);
        struct desk_rect track = desk_master_track(p, fonts->value);
        int value_h = fonts->value ? font_height(fonts->value) : 15;
        // Pin the old drawn rectangle independently of the extracted helper.
        assert(track.x == 876 && track.w == 116);
        assert(track.y == 56 + 30 + value_h + 8 + 12);
        assert(track.y + track.h == 412);
        int x = p->x + p->w / 2;
        int top = track.y + 12, bottom = track.y + track.h - 12;
        printf("master %s: text_h=%d, track=(%d,%d,%d,%d), centre travel=%d..%d\n",
               fallback ? "fallback" : "Inter", value_h, track.x, track.y,
               track.w, track.h, top, bottom);

        // The track still paints at its old bounds, with nothing moved above it.
        desk_paint(&c, &m, fonts);
        assert(c.px[(track.y - 1) * c.w + x] == DESK_TILE);
        assert(c.px[track.y * c.w + x] == DESK_GLASS);
        assert(c.px[(track.y + track.h - 1) * c.w + x] == DESK_GLASS);
        assert(c.px[(track.y + track.h) * c.w + x] == DESK_TILE);

        int ys[] = {track.y, track.y + track.h - 1, track.y + track.h / 2,
                    top + 8, bottom - 8, p->y, p->y + p->h - 1};
        int expected[] = {255, 0, 128, 255, 0, 255, 0};
        for (size_t i = 0; i < sizeof ys / sizeof ys[0]; i++) {
            // In particular, full level is reached well below the tile's top,
            // with no need to drag towards the lock in the bar.
            assert(ys[i] >= p->y && ys[i] < p->y + p->h);
            struct desk_action a = desk_touch_down(&m, 0, x, ys[i], &track);
            assert(a.kind == DESK_ACT_MASTER && a.widget_id == 42);
            assert(abs(a.value - expected[i]) <= (i == 2 ? 1 : 0));
            desk_touch_up(&m, 0, x, ys[i]);
        }
        assert(track.y > p->y && top + 8 < p->y + p->h);

        // A touch in the tile's side margin still captures the entire drag.
        assert(desk_touch_down(&m, 0, p->x + 1, track.y, &track).value == 255);
        assert(desk_touch_move(&m, 1, x, bottom, &track).kind == DESK_ACT_NONE);
        int previous = 255;
        for (int y = track.y; y < track.y + track.h; y++) {
            struct desk_action a = desk_touch_move(&m, 0, x, y, &track);
            int level = m.control[im].requested_level;
            assert(level >= 0 && level <= previous);
            assert(a.kind == (level == previous ? DESK_ACT_NONE : DESK_ACT_MASTER));
            if (a.kind == DESK_ACT_MASTER)
                assert(a.value == level);
            if (y <= top + 8)
                assert(level == 255);
            else if (y >= bottom - 8)
                assert(level == 0);
            else {
                assert(level > 0 && level < 255);
                // A confirmed push draws the grip centre under the finger.
                desk_apply_master(&m, level);
                desk_paint(&c, &m, fonts);
                int thumb_y = -1;
                for (int py = track.y; py < track.y + track.h; py++) {
                    if (c.px[py * c.w + x] == DESK_INK) {
                        thumb_y = py;
                        break;
                    }
                }
                assert(thumb_y >= 0 && abs(thumb_y + 12 - y) <= 1);
            }
            previous = level;
        }
        assert(previous == 0);
        assert(desk_touch_move(&m, 0, x, p->y - 100, &track).value == 255);
        assert(desk_touch_move(&m, 0, x, p->y + p->h + 100, &track).value == 0);
        desk_touch_up(&m, 0, x, p->y + p->h);
    }
    free(c.px);
    desk_fonts_close(&loaded);
    puts("desk_master_travel ok");
    return 0;
}
