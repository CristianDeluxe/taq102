#include "desk_show_layout.h"

#include <stdio.h>
#include <string.h>

#include "desk_layout.h"

// The rows, from the top of the content.
#define ROW_STATES_Y 76
#define ROW_HITS_Y 200
#define ROW_FOG_Y 340
#define ROW_COLOUR_Y 464
#define HEADING_ABOVE 20

static int add(struct desk_layout *out, int control, int page, int x, int y, int w, int h,
               enum desk_tile tile) {
    if (out->placements >= DESK_MAX_PLACEMENTS)
        return -1;
    struct desk_placement *p = &out->placement[out->placements++];
    *p = (struct desk_placement){ control, page, 0, x, y, w, h, tile };
    return 0;
}

static int heading(struct desk_layout *out, int page, int y, const char *text, int section) {
    if (out->headings >= DESK_MAX_HEADINGS)
        return -1;
    struct desk_heading *h = &out->heading[out->headings++];
    h->page = page;
    h->bank = 0;
    h->x = DESK_CONTENT_X;
    h->y = y;
    h->w = DESK_CONTENT_W;
    snprintf(h->text, sizeof h->text, "%s", text);
    h->section = section;
    h->part = h->parts = 1;
    return 0;
}

int desk_show_live_page(const struct show_map *map) {
    for (int i = 0; i < map->pages; i++)
        if (strcmp(map->page[i].key, "live") == 0)
            return i;
    return -1;
}

static const struct map_section *section_of(const struct show_map *map, int page, const char *key) {
    if (page < 0)
        return NULL;
    for (int s = 0; s < map->page[page].sections; s++)
        if (strcmp(map->page[page].section[s].key, key) == 0)
            return &map->page[page].section[s];
    return NULL;
}

static int section_index(const struct show_map *map, int page, const struct map_section *sec) {
    return (int)(sec - map->page[page].section);
}

static int is_fog(const struct map_control *c) {
    return strncmp(c->key, "humo", 4) == 0;
}

int desk_show_compose(const struct show_map *map, int page, int haze_off, int tempo,
                      struct desk_layout *out) {
    int live = desk_show_live_page(map);
    if (live < 0)
        return -1;
    const struct map_section *states = section_of(map, live, "state");
    const struct map_section *haze = section_of(map, live, "haze");
    const struct map_section *accents = section_of(map, live, "accents");

    // Row one: the states, seven across.
    if (states) {
        if (heading(out, page, ROW_STATES_Y - HEADING_ABOVE, states->title, section_index(map, live, states)) != 0)
            return -1;
        int x = DESK_CONTENT_X;
        for (int i = states->first; i < states->first + states->count; i++) {
            if (x + DESK_STATE_W > DESK_CONTENT_X + DESK_CONTENT_W)
                break;
            if (add(out, i, page, x, ROW_STATES_Y, DESK_STATE_W, DESK_STATE_H, TILE_STATE) != 0)
                return -1;
            x += DESK_STATE_W + DESK_GAP;
        }
    }
    // Row two: the hits that fire while held, then any toggle among them.
    // Row three: the fog holds. Both from the accents section.
    int hits_x = DESK_CONTENT_X, fog_x = DESK_CONTENT_X, toggles_x = -1;
    if (accents) {
        if (heading(out, page, ROW_HITS_Y - HEADING_ABOVE, "Golpes \xc2\xb7 mientras pulses",
                    section_index(map, live, accents)) != 0)
            return -1;
        out->heading[out->headings - 1].w = 5 * (DESK_SHOW_HOLD_W + DESK_GAP) - DESK_GAP;
        if (heading(out, page, ROW_HITS_Y - HEADING_ABOVE, "Fijo", section_index(map, live, accents)) != 0)
            return -1;
        out->heading[out->headings - 1].x = DESK_CONTENT_X + 5 * (DESK_SHOW_HOLD_W + DESK_GAP);
        out->heading[out->headings - 1].w = DESK_CONTENT_W - 5 * (DESK_SHOW_HOLD_W + DESK_GAP);
        for (int i = accents->first; i < accents->first + accents->count; i++) {
            const struct map_control *c = &map->control[i];
            if (c->role == MAP_ROLE_ACCENT && is_fog(c)) {
                if (fog_x + DESK_FOG_W > DESK_CONTENT_X + 336)
                    continue;
                if (add(out, i, page, fog_x, ROW_FOG_Y, DESK_FOG_W, DESK_FOG_H, TILE_FOG) != 0)
                    return -1;
                fog_x += DESK_FOG_W + DESK_GAP;
            } else if (c->role == MAP_ROLE_ACCENT) {
                if (hits_x + DESK_SHOW_HOLD_W > DESK_CONTENT_X + 5 * (DESK_SHOW_HOLD_W + DESK_GAP))
                    continue;
                if (add(out, i, page, hits_x, ROW_HITS_Y, DESK_SHOW_HOLD_W, DESK_HOLD_TILE_H, TILE_HOLD) != 0)
                    return -1;
                hits_x += DESK_SHOW_HOLD_W + DESK_GAP;
            }
        }
        toggles_x = DESK_CONTENT_X + 5 * (DESK_SHOW_HOLD_W + DESK_GAP);
        for (int i = accents->first; i < accents->first + accents->count; i++) {
            const struct map_control *c = &map->control[i];
            if (c->role != MAP_ROLE_ACCENT) {
                if (toggles_x + DESK_SHOW_HOLD_W > DESK_CONTENT_X + DESK_CONTENT_W)
                    break;
                if (add(out, i, page, toggles_x, ROW_HITS_Y, DESK_SHOW_HOLD_W, DESK_HOLD_TILE_H, TILE_CUE) != 0)
                    return -1;
                toggles_x += DESK_SHOW_HOLD_W + DESK_GAP;
            }
        }
    }
    // Row three, right: the ambient selector, OFF first, under its own
    // words: choosing a rhythm fires at once, OFF stops the cycle.
    if (heading(out, page, ROW_FOG_Y - HEADING_ABOVE, "Humo a mano", accents ? section_index(map, live, accents) : 0) != 0)
        return -1;
    out->heading[out->headings - 1].w = 320;
    if (haze) {
        if (heading(out, page, ROW_FOG_Y - HEADING_ABOVE, "Ambiente \xc2\xb7 dispara al elegir \xc2\xb7 OFF lo para",
                    (int)(haze - map->page[live].section)) != 0)
            return -1;
        out->heading[out->headings - 1].x = DESK_CONTENT_X + 336;
        out->heading[out->headings - 1].w = DESK_CONTENT_W - 336;
        int x = DESK_CONTENT_X + 336;
        int y = ROW_FOG_Y;
        if (haze_off >= 0) {
            if (add(out, haze_off, page, x, y, DESK_SEGMENT_W, DESK_SEGMENT_H, TILE_SEGMENT) != 0)
                return -1;
            x += DESK_SEGMENT_W + DESK_SEGMENT_GAP;
        }
        for (int i = haze->first; i < haze->first + haze->count; i++) {
            if (x + DESK_SEGMENT_W > DESK_CONTENT_X + DESK_CONTENT_W)
                break;
            if (add(out, i, page, x, y, DESK_SEGMENT_W, DESK_SEGMENT_H, TILE_SEGMENT) != 0)
                return -1;
            x += DESK_SEGMENT_W + DESK_SEGMENT_GAP;
        }
    }
    // Row four: the rig's single colours from COLOR, and the tempo card.
    if (heading(out, page, ROW_COLOUR_Y - HEADING_ABOVE, "Color del rig", 0) != 0)
        return -1;
    int x = DESK_CONTENT_X;
    for (int i = 0; i < map->count; i++) {
        const struct map_control *c = &map->control[i];
        if (c->role != MAP_ROLE_PICK || strncmp(c->key, "rig-", 4) != 0 || c->swatches != 1)
            continue;
        if (x + DESK_MINI_W > DESK_CONTENT_X + 10 * (DESK_MINI_W + DESK_GAP))
            break;
        if (add(out, i, page, x, ROW_COLOUR_Y, DESK_MINI_W, DESK_MINI_H, TILE_MINI) != 0)
            return -1;
        x += DESK_MINI_W + DESK_GAP;
    }
    if (tempo >= 0 &&
        add(out, tempo, page, DESK_CONTENT_X + DESK_CONTENT_W - DESK_TEMPO_W, ROW_COLOUR_Y,
            DESK_TEMPO_W, DESK_TEMPO_H, TILE_TEMPO) != 0)
        return -1;
    return 0;
}
