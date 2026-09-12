// SOURCES: showmap.c showmap_validate.c desk_model.c vcjson.c
// The generated map (schema 2) parsed with its bounds, and built against the
// console the master really serves: the room's states pressable, the held
// hits carried disabled, the panic button live, and a console that is not
// the show disabling everything with one reason.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desk_layout.h"
#include "desk_model.h"
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

// The map text with one substring replaced, for the refusals.
static char *edited(const char *text, const char *from, const char *to) {
    const char *at = strstr(text, from);
    assert(at);
    size_t before = (size_t)(at - text), flen = strlen(from), tlen = strlen(to);
    char *out = malloc(strlen(text) - flen + tlen + 1);
    assert(out);
    memcpy(out, text, before);
    memcpy(out + before, to, tlen);
    strcpy(out + before + tlen, at + flen);
    return out;
}

static const struct desk_control *by_label(const struct desk_model *m, const char *label) {
    for (int i = 0; i < m->count; i++)
        if (strcmp(m->control[i].label, label) == 0)
            return &m->control[i];
    return NULL;
}

static const struct desk_control *by_kind(const struct desk_model *m, enum desk_kind kind) {
    for (int i = 0; i < m->count; i++)
        if (m->control[i].kind == kind)
            return &m->control[i];
    return NULL;
}

int main(void) {
    size_t map_len, vc_len;
    char *map_text = slurp("show/vibra.desk.json", &map_len);
    char *vc_text = slurp("tests/dmx-desk/fixtures/vc-vibra.json", &vc_len);

    // The map, as generated.
    struct show_map map;
    assert(showmap_parse(map_text, map_len, &map) == 0);
    assert(map.schema == 2 && strcmp(map.qlc_version, "5.2.2") == 0);
    assert(strcmp(map.key, "vibra") == 0);
    assert(map.stop_all_widget == 21 && map.stop_all_fade_ms == 1000);
    assert(map.grand_master_widget == 246);
    assert(map.pages == 7 && strcmp(map.page[0].key, "live") == 0);
    const struct map_section *state = &map.page[0].section[0];
    assert(strcmp(state->key, "state") == 0 && state->count == 7 && state->solo_id == 3);
    assert(state->first == 0);
    const struct map_control *auto_ctl = &map.control[state->first];
    assert(strcmp(auto_ctl->caption, "AUTO") == 0 && auto_ctl->widget_id == 4 &&
           auto_ctl->function_id == 720 && auto_ctl->role == MAP_ROLE_STATE &&
           auto_ctl->solo_id == 3 && auto_ctl->page == 0 && auto_ctl->section == 0);
    assert(strcmp(auto_ctl->detail, "el show se lleva solo") == 0);
    const struct map_section *accents = &map.page[0].section[1];
    assert(accents->count == 8);
    int held = 0;
    for (int i = 0; i < accents->count; i++) {
        const struct map_control *c = &map.control[accents->first + i];
        if (c->held) {
            held++;
            assert(!c->enabled && c->reason[0]);
        }
    }
    assert(held == 7);
    // A colour pick carries the colour its scenes write.
    int red_found = 0;
    for (int i = 0; i < map.count; i++) {
        if (strcmp(map.control[i].caption, "Rig Rojo") == 0) {
            assert(map.control[i].swatches >= 1 && map.control[i].swatch[0] == 0xff0000u);
            assert(map.control[i].role == MAP_ROLE_PICK);
            red_found = 1;
        }
    }
    assert(red_found);
    assert(map.count == 132);

    // Refusals: a widget listed twice, a section naming a missing control, a
    // swatch that is not a colour, a wrong schema.
    struct show_map bad;
    char *dup = edited(map_text, "\"widget\": 5,", "\"widget\": 4,");
    assert(showmap_parse(dup, strlen(dup), &bad) == -1);
    free(dup);
    char *missing = edited(map_text, "\"auto\",", "\"auto\", \"nobody\",");
    assert(showmap_parse(missing, strlen(missing), &bad) == -1);
    free(missing);
    char *swatch = edited(map_text, "\"#ff0000\"", "\"red\"");
    assert(showmap_parse(swatch, strlen(swatch), &bad) == -1);
    free(swatch);
    char *schema = edited(map_text, "\"schema\": 2", "\"schema\": 1");
    assert(showmap_parse(schema, strlen(schema), &bad) == -1);
    free(schema);

    // Built against the real console: seven states, the master, the panic
    // button; the held hits disabled with the map's reason.
    struct vc_doc console;
    assert(vc_parse(vc_text, vc_len, &console) == 0);
    struct desk_model model;
    int enabled = showmap_build(&model, &map, &console);
    assert(model.count == 134);
    const struct desk_control *a = by_label(&model, "AUTO");
    assert(a && a->enabled && a->widget_id == 4 && a->function_id == 720);
    assert(a->w == DESK_TILE_W && a->x == DESK_GRID_X && a->y == DESK_GRID_Y);
    const struct desk_control *negro = by_label(&model, "TODO NEGRO");
    assert(negro && negro->enabled && negro->x == DESK_GRID_X && negro->y == DESK_GRID_Y + 2 * (DESK_TILE_H + DESK_GAP));
    const struct desk_control *flash = by_label(&model, "FLASH");
    assert(flash && !flash->enabled && strstr(flash->reason, "held"));
    assert(flash->w == 0);                       // carried, not on this screen
    const struct desk_control *rojo = by_label(&model, "Rig Rojo");
    assert(rojo && rojo->enabled && rojo->w == 0 && rojo->swatches == 1);
    const struct desk_control *stop = by_kind(&model, DESK_STOP_ALL);
    assert(stop && stop->enabled && stop->widget_id == 21 && stop->y == DESK_PANIC_Y);
    assert(strcmp(stop->detail, "1.0 s fade") == 0);
    const struct desk_control *master = by_kind(&model, DESK_MASTER);
    assert(master && master->enabled && master->state == DESK_UNKNOWN);
    // Everything but the seventeen held hits (seven on LIVE, ten colour
    // golpes) is enabled: 115, plus the master and the panic button.
    assert(enabled == 115 + 2);

    // The panic button is a completed tap when READY, and nothing otherwise.
    int sx = stop->x + 10, sy = stop->y + 10;
    assert(desk_touch_down(&model, 0, sx, sy).kind == DESK_ACT_NONE);
    assert(desk_touch_up(&model, 0, sx, sy).kind == DESK_ACT_NONE);
    desk_set_link(&model, DESK_LINK_READY);
    assert(desk_touch_down(&model, 0, sx, sy).kind == DESK_ACT_NONE);
    struct desk_action panic = desk_touch_up(&model, 0, sx, sy);
    assert(panic.kind == DESK_ACT_STOP_ALL && panic.widget_id == 21);
    // A control with no place on screen cannot be hit at (0,0).
    assert(desk_touch_down(&model, 0, 0, 0).kind == DESK_ACT_NONE);

    // A widget that drives another function than the map says is dead.
    char *other = edited(vc_text, "\"functionId\":720", "\"functionId\":721");
    struct vc_doc changed;
    assert(vc_parse(other, strlen(other), &changed) == 0);
    showmap_build(&model, &map, &changed);
    a = by_label(&model, "AUTO");
    assert(a && !a->enabled && strcmp(a->reason, "drives another cue") == 0);
    vc_free(&changed);
    free(other);

    // Another QLC+ line is another show: everything off, one reason.
    char *newer = edited(vc_text, "\"version\":\"5.2.2\"", "\"version\":\"5.3.0\"");
    assert(vc_parse(newer, strlen(newer), &changed) == 0);
    assert(showmap_build(&model, &map, &changed) == 0);
    for (int i = 0; i < model.count; i++)
        assert(!model.control[i].enabled && strcmp(model.control[i].reason, "show mismatch") == 0);
    vc_free(&changed);
    free(newer);

    vc_free(&console);
    free(map_text);
    free(vc_text);
    printf("showmap v2 ok\n");
    return 0;
}
