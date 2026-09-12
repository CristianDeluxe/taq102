// SOURCES: desk_model.c desk_paint.c showmap.c showmap_validate.c vcjson.c canvas.c canvas_blend.c font.c
// The desk, built from the generated Vibra map against the real console
// document, pressed once, and painted. Writes $TEST_OUT/desk.ppm so the screen can be
// looked at on a laptop before it reaches the tablet.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "desk_layout.h"
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

    // Every tile that has a place lands on the grid, inside the chrome, and
    // never under the master.
    for (int i = 0; i < model.count; i++) {
        const struct desk_control *c = &model.control[i];
        if (c->w == 0)
            continue;
        assert(c->x >= DESK_RAIL_W && c->y >= DESK_BAR_H);
        assert(c->x + c->w <= DESK_W && c->y + c->h <= DESK_H);
        if (c->kind == DESK_CUE)
            assert(c->x + c->w <= DESK_MASTER_X);
    }

    // Every enabled tile starts from the console document's own state rather
    // than from nothing: in this fixture nothing was running.
    assert(by_label(&model, "AUTO")->state == DESK_OFF);

    // Nothing is pressable while the link is down, however good the map is.
    const struct desk_control *auto_ctl = by_label(&model, "AUTO");
    int cx = auto_ctl->x + auto_ctl->w / 2, cy = auto_ctl->y + auto_ctl->h / 2;
    assert(desk_touch_down(&model, 0, cx, cy).kind == DESK_ACT_NONE);
    assert(desk_touch_up(&model, 0, cx, cy).kind == DESK_ACT_NONE);

    desk_set_link(&model, DESK_LINK_READY);
    assert(desk_touch_down(&model, 0, cx, cy).kind == DESK_ACT_NONE);
    assert(by_label(&model, "AUTO")->pressed == 1);
    // A second finger cannot fire another tile while the first is captured.
    const struct desk_control *charla = by_label(&model, "CHARLA");
    assert(desk_touch_down(&model, 1, charla->x + 10, charla->y + 10).kind == DESK_ACT_NONE);
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
    };
    struct canvas canvas = { calloc(DESK_W * DESK_H, 4), DESK_W, DESK_H };
    assert(canvas.px);
    desk_paint(&canvas, &model, &fonts);

    const char *out = getenv("TEST_OUT");
    char path[512];
    snprintf(path, sizeof path, "%s/desk.ppm", out ? out : "/tmp");
    write_ppm(&canvas, path);
    printf("wrote %s\n", path);

    // And the same screen with the link down, which is what a gig sees when
    // the Wi-Fi goes.
    desk_set_link(&model, DESK_LINK_DOWN);
    desk_paint(&canvas, &model, &fonts);
    snprintf(path, sizeof path, "%s/desk-nolink.ppm", out ? out : "/tmp");
    write_ppm(&canvas, path);

    font_close(fonts.tile);
    font_close(fonts.value);
    font_close(fonts.label);
    free(canvas.px);
    vc_free(&console);
    return 0;
}
