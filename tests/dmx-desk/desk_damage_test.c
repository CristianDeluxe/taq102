// SOURCES: desk_model.c desk_master_level_at.c desk_layout_resolve.c desk_show_layout.c showmap.c showmap_validate.c vcjson.c
// What changed is what gets painted: a push lights one tile's rectangle, a
// view change everything, and a clip keeps a primitive inside its damage.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desk_layout.h"
#include "desk_layout_resolve.h"
#include "desk_model.h"
#include "showmap.h"
#include "showmap_validate.h"
#include "vcjson.h"

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    assert(fread(b, 1, (size_t)n, f) == (size_t)n);
    b[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return b;
}

int main(void) {
    size_t n;
    char *vc = slurp("tests/dmx-desk/fixtures/vc-vibra.json", &n);
    struct vc_doc doc;
    assert(vc_parse(vc, n, &doc) == 0);
    struct show_map map;
    assert(showmap_load("show/vibra.desk.json", &map) == 0);
    struct desk_model m;
    showmap_build(&m, &map, &doc);
    struct desk_layout layout;
    assert(desk_layout_resolve(&map, map.count, map.count + 1, map.count + 2, map.count + 3, &layout) == 0);
    desk_set_layout(&m, &layout);
    desk_set_link(&m, DESK_LINK_READY);

    int x, y, w, h;
    // After setup everything is damaged, once.
    assert(desk_take_damage(&m, &x, &y, &w, &h) == 1 && w < 0);
    assert(desk_take_damage(&m, &x, &y, &w, &h) == 0);

    // A push lights AUTO: its rectangle on LIVE, nothing else.
    desk_apply_function(&m, 720, 1);
    assert(desk_take_damage(&m, &x, &y, &w, &h) == 1);
    int auto_ix = -1;
    for (int i = 0; i < m.count; i++)
        if (strcmp(m.control[i].label, "AUTO") == 0)
            auto_ix = i;
    const struct desk_placement *ap = desk_placement_of(&m, auto_ix);
    assert(ap && x == ap->x && y == ap->y && w == ap->w && h == ap->h);

    // A push for a control on another page damages nothing on this view.
    int gobo_fn = -1;
    for (int i = 0; i < m.count; i++)
        if (strcmp(m.control[i].label, "Gobo 1") == 0)
            gobo_fn = m.control[i].function_id;
    assert(gobo_fn >= 0);
    desk_apply_function(&m, gobo_fn, 1);
    assert(desk_take_damage(&m, &x, &y, &w, &h) == 0);

    // Two changes: the union.
    desk_apply_function(&m, 720, 0);
    desk_apply_function(&m, 723, 1);
    assert(desk_take_damage(&m, &x, &y, &w, &h) == 1);
    assert(x == ap->x && y == ap->y && w > ap->w && h == ap->h);

    // A view change is everything; a status rectangle is itself.
    desk_set_view(&m, 1, 0);
    assert(desk_take_damage(&m, &x, &y, &w, &h) == 1 && w < 0);
    desk_damage_rect(&m, 0, 0, 1024, 48);
    assert(desk_take_damage(&m, &x, &y, &w, &h) == 1 && x == 0 && y == 0 && w == 1024 && h == 48);

    vc_free(&doc);
    free(vc);
    printf("desk_damage ok\n");
    return 0;
}
