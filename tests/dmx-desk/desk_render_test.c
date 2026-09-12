// SOURCES: desk_model.c desk_paint.c desk_caption.c desk_view.c desk_layout_resolve.c showmap.c showmap_validate.c vcjson.c canvas.c canvas_blend.c font.c
// The desk, built from the generated Vibra map against the real console
// document, pressed once, and painted. Writes $TEST_OUT/desk.ppm so the screen can be
// looked at on a laptop before it reaches the tablet.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "desk_layout.h"
#include "desk_layout_resolve.h"
#include "desk_model.h"
#include "desk_paint.h"
#include "showmap.h"
#include "showmap_validate.h"
#include "vcjson.h"

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    assert(size > 0);
    assert(fseek(f, 0, SEEK_SET) == 0);
    char *buf = malloc((size_t)size + 1);
    assert(buf);
    assert(fread(buf, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    buf[size] = '\0';
    *len = (size_t)size;
    return buf;
}

static const struct desk_control *by_label(const struct desk_model *m,
                                           const char *label) {
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->control[i].label, label) == 0)
            return &m->control[i];
    }
    return NULL;
}

static void write_ppm(const struct canvas *c, const char *path) {
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        unsigned char rgb[3] = { (p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(void) {
    size_t len;
    char *json = slurp("tests/dmx-desk/fixtures/vc-vibra.json", &len);
    struct vc_doc console;
    assert(vc_parse(json, len, &console) == 0);
    free(json);

    struct show_map map;
    assert(showmap_load("show/vibra.desk.json", &map) == 0);
    assert(strcmp(map.key, "vibra") == 0);

    struct desk_model model;
    int enabled = showmap_build(&model, &map, &console);
    assert(enabled > 100);
    struct desk_layout layout;
    assert(desk_layout_resolve(&map, map.count, map.count + 1, &layout) == 0);
    desk_set_layout(&model, &layout);

    // Every placement lands inside the chrome, and cues never under the master.
    for (int i = 0; i < layout.placements; i++) {
        const struct desk_placement *p = &layout.placement[i];
        assert(p->x >= DESK_RAIL_W && p->y >= DESK_BAR_H);
        assert(p->x + p->w <= DESK_W && p->y + p->h <= DESK_H);
        if (p->tile != TILE_MASTER && p->tile != TILE_PANIC)
            assert(p->x + p->w <= DESK_MASTER_X);
    }

    // Every enabled tile starts from the console document's own state rather
    // than from nothing: in this fixture nothing was running.
    assert(by_label(&model, "AUTO")->state == DESK_OFF);

    // Nothing is pressable while the link is down, however good the map is.
    const struct desk_control *auto_ctl = by_label(&model, "AUTO");
    const struct desk_placement *ap = desk_placement_of(&model, (int)(auto_ctl - model.control));
    int cx = ap->x + ap->w / 2, cy = ap->y + ap->h / 2;
    assert(desk_touch_down(&model, 0, cx, cy).kind == DESK_ACT_NONE);
    assert(desk_touch_up(&model, 0, cx, cy).kind == DESK_ACT_NONE);

    desk_set_link(&model, DESK_LINK_READY);
    assert(desk_touch_down(&model, 0, cx, cy).kind == DESK_ACT_NONE);
    assert(by_label(&model, "AUTO")->pressed == 1);
    // A second finger cannot fire another tile while the first is captured.
    const struct desk_control *charla = by_label(&model, "CHARLA");
    const struct desk_placement *chp = desk_placement_of(&model, (int)(charla - model.control));
    assert(desk_touch_down(&model, 1, chp->x + 10, chp->y + 10).kind == DESK_ACT_NONE);
    assert(by_label(&model, "CHARLA")->pressed == 0);
    // One gesture, one message, on release, and the tile does not light itself.
    struct desk_action fired = desk_touch_up(&model, 0, cx, cy);
    assert(fired.kind == DESK_ACT_TOGGLE && fired.widget_id == 4);
    assert(by_label(&model, "AUTO")->state == DESK_OFF);

    // Sliding off a cue before letting go sends nothing.
    assert(desk_touch_down(&model, 0, cx, cy).kind == DESK_ACT_NONE);
    desk_touch_move(&model, 0, 5, 5);
    assert(desk_touch_up(&model, 0, 5, 5).kind == DESK_ACT_NONE);

    // The show's word, not the finger's: only this lights a tile.
    desk_apply_function(&model, 720, 1);
    assert(by_label(&model, "AUTO")->state == DESK_ON);
    desk_apply_function(&model, 723, 1);
    assert(by_label(&model, "CHARLA")->state == DESK_ON);

    // Losing the link takes every claim about the rig with it.
    desk_set_link(&model, DESK_LINK_DOWN);
    assert(by_label(&model, "AUTO")->state == DESK_UNKNOWN);

    desk_set_link(&model, DESK_LINK_READY);
    desk_apply_function(&model, 720, 1);
    desk_apply_master(&model, 200);

    struct desk_fonts fonts = {
        font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf", 22),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf", 56),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-Regular.ttf", 20),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-Regular.ttf", 16),
    };
    struct canvas canvas = { calloc(DESK_W * DESK_H, 4), DESK_W, DESK_H };
    assert(canvas.px);
    desk_paint(&canvas, &model, &fonts);

    const char *out = getenv("TEST_OUT");
    char path[512];
    snprintf(path, sizeof path, "%s/desk.ppm", out ? out : "/tmp");
    write_ppm(&canvas, path);
    printf("wrote %s\n", path);

    // The COLOR page with the pick lit, and the gobos' second bank.
    desk_set_view(&model, 1, 0);
    desk_apply_function(&model, 727, 1);
    desk_paint(&canvas, &model, &fonts);
    snprintf(path, sizeof path, "%s/desk-color.ppm", out ? out : "/tmp");
    write_ppm(&canvas, path);
    desk_set_view(&model, 4, 1);
    desk_paint(&canvas, &model, &fonts);
    snprintf(path, sizeof path, "%s/desk-gobos.ppm", out ? out : "/tmp");
    write_ppm(&canvas, path);
    desk_set_view(&model, 0, 0);

    // And the same screen with the link down, which is what a gig sees when
    // the Wi-Fi goes.
    desk_set_link(&model, DESK_LINK_DOWN);
    desk_paint(&canvas, &model, &fonts);
    snprintf(path, sizeof path, "%s/desk-nolink.ppm", out ? out : "/tmp");
    write_ppm(&canvas, path);

    font_close(fonts.tile);
    font_close(fonts.value);
    font_close(fonts.label);
    font_close(fonts.small);
    free(canvas.px);
    vc_free(&console);
    return 0;
}
