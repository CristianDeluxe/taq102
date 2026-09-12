// SOURCES: desk_speed.c desk_speed_paint.c desk_tap.c speed_factor.c showmap.c vcjson.c canvas.c canvas_blend.c font.c
// The SPEED page's cards painted: both dials known from the console, one
// change waiting, one noted; then the page before anything is known.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desk_layout.h"
#include "desk_speed.h"
#include "desk_speed_layout.h"
#include "desk_speed_paint.h"
#include "font.h"

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    assert(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    buf[n] = '\0';
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void save(const struct canvas *c, const char *name) {
    const char *out = getenv("TEST_OUT");
    char path[512];
    snprintf(path, sizeof path, "%s/%s", out ? out : "/tmp", name);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        unsigned char rgb[3] = { (p >> 16) & 255, (p >> 8) & 255, p & 255 };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s\n", path);
}

int main(void) {
    size_t map_len, vc_len;
    char *map_text = slurp("show/vibra.desk.json", &map_len);
    char *vc_text = slurp("tests/dmx-desk/fixtures/vc-vibra.json", &vc_len);
    struct show_map map;
    assert(showmap_parse(map_text, map_len, &map) == 0);
    struct vc_doc console;
    assert(vc_parse(vc_text, vc_len, &console) == 0);
    struct desk_fonts fonts = {
        font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf", 22),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf", 56),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-Regular.ttf", 20),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-Regular.ttf", 16),
    };
    struct canvas canvas = { .px = calloc(DESK_W * DESK_H, 4), .w = DESK_W, .h = DESK_H };
    assert(canvas.px);
    for (int i = 0; i < DESK_W * DESK_H; i++)
        canvas.px[i] = DESK_GLASS;

    struct desk_speed s;
    desk_speed_init(&s, &map);
    desk_speed_paint(&canvas, &s, &fonts);
    save(&canvas, "speed-unknown.ppm");

    desk_speed_validate(&s, &console);
    desk_speed_apply(&s, 274, 400, 7, 1000);          // "State updated" on the second
    desk_speed_touch_down(&s, SPEED_CARD_X + SPEED_CELL_X(1) + 10, SPEED_CARD_Y(0) + SPEED_CELL_Y(0) + 10, 1000, 1000);
    desk_speed_touch_up(&s, SPEED_CARD_X + SPEED_CELL_X(1) + 10, SPEED_CARD_Y(0) + SPEED_CELL_Y(0) + 10, 1000);
    assert(s.dial[0].pending);
    desk_speed_paint(&canvas, &s, &fonts);
    save(&canvas, "speed.ppm");
    // The cards sit in the content only.
    assert(canvas.px[(SPEED_CARD_Y(0) + 40) * DESK_W + SPEED_CARD_X + 40] == DESK_TILE);
    assert(canvas.px[SPEED_CARD_Y(0) * DESK_W + 900] == DESK_GLASS);
    assert(canvas.px[100 * DESK_W + 400] == DESK_GLASS);
    printf("speed_render ok\n");
    vc_free(&console);
    free(map_text);
    free(vc_text);
    return 0;
}
