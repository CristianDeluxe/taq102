// SOURCES: desk_layout_resolve.c showmap.c
// The resolver over the real map: every control placed, nothing overlapping,
// nothing outside the content area, the banks as the arithmetic yields them.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_layout.h"
#include "desk_layout_resolve.h"
#include "showmap.h"

static int overlap(const struct desk_placement *a, const struct desk_placement *b) {
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static const struct desk_placement *find(const struct desk_layout *l, int control, int page) {
    for (int i = 0; i < l->placements; i++)
        if (l->placement[i].control == control && l->placement[i].page == page)
            return &l->placement[i];
    return NULL;
}

static int control_named(const struct show_map *map, const char *caption) {
    for (int i = 0; i < map->count; i++)
        if (strcmp(map->control[i].caption, caption) == 0)
            return i;
    return -1;
}

int main(void) {
    struct show_map map;
    assert(showmap_load("show/vibra.desk.json", &map) == 0);
    int master = map.count, panic = map.count + 1;
    struct desk_layout l;
    assert(desk_layout_resolve(&map, master, panic, &l) == 0);
    assert(l.pages == 8 && l.speed_page == 7 && strcmp(l.title[7], "SPEED") == 0);

    // Every control has a placement on its own page; the states also on every
    // other page, compact.
    for (int i = 0; i < map.count; i++) {
        const struct desk_placement *p = find(&l, i, map.control[i].page);
        assert(p);
        // On LIVE only the held hits take the compact form.
        if (map.control[i].page == 0)
            assert(p->tile != TILE_COMPACT || (map.control[i].role == MAP_ROLE_ACCENT && map.control[i].held));
    }
    int auto_ix = control_named(&map, "AUTO");
    for (int page = 1; page < l.pages; page++) {
        const struct desk_placement *p = find(&l, auto_ix, page);
        assert(p && p->tile == TILE_COMPACT && p->bank == 0 && p->y == DESK_GRID_Y && p->w == 86);
    }
    // AUTO is the wide tile on LIVE, first row, with CHARLA beside it.
    const struct desk_placement *a = find(&l, auto_ix, 0);
    assert(a->tile == TILE_WIDE && a->w == 452 && a->x == DESK_GRID_X && a->y == DESK_GRID_Y + 32 && a->h == 88);
    const struct desk_placement *ch = find(&l, control_named(&map, "CHARLA"), 0);
    assert(ch->y == a->y && ch->x == a->x + 452 + 16 && ch->w == 218);
    // The haze tiles are four to a row.
    const struct desk_placement *h1 = find(&l, control_named(&map, "cada 1 min"), 0);
    const struct desk_placement *h4 = find(&l, control_named(&map, "cada 8 min"), 0);
    assert(h1 && h4 && h1->tile == TILE_HAZE && h1->y == h4->y && h4->x + h4->w <= DESK_MASTER_X);
    // Picks are swatch tiles, six to a row.
    const struct desk_placement *rojo = find(&l, control_named(&map, "Rig Rojo"), 1);
    assert(rojo && rojo->tile == TILE_SWATCH && rojo->w == 100);

    // Bounds and overlaps, per page and bank; the master and the panic button
    // once per bank.
    for (int page = 0; page < l.pages; page++) {
        assert(l.banks[page] >= 1 && l.banks[page] <= DESK_MAX_BANKS);
        int end = l.banks[page] > 1 ? DESK_H - 56 : DESK_H;
        for (int bank = 0; bank < l.banks[page]; bank++) {
            int masters = 0, panics = 0;
            for (int i = 0; i < l.placements; i++) {
                const struct desk_placement *p = &l.placement[i];
                if (p->page != page || p->bank != bank)
                    continue;
                if (p->tile == TILE_MASTER) { masters++; continue; }
                if (p->tile == TILE_PANIC) { panics++; continue; }
                assert(p->x >= DESK_GRID_X && p->x + p->w <= DESK_MASTER_X);
                assert(p->y >= DESK_GRID_Y && p->y + p->h <= end);
                for (int j = i + 1; j < l.placements; j++) {
                    const struct desk_placement *q = &l.placement[j];
                    if (q->page == page && q->bank == bank && q->tile != TILE_MASTER &&
                        q->tile != TILE_PANIC)
                        assert(!overlap(p, q));
                }
            }
            assert(masters == 1 && panics == 1);
        }
    }
    // Headings: the gobo picks are split across banks with part/parts.
    int gobo_parts = 0;
    for (int i = 0; i < l.headings; i++) {
        const struct desk_heading *h = &l.heading[i];
        if (h->page == 4 && strcmp(h->text, "ELEGIR") == 0) {
            assert(h->parts >= 2 && h->part >= 1 && h->part <= h->parts);
            gobo_parts++;
        }
    }
    assert(gobo_parts >= 2);
    printf("banks per page:");
    for (int page = 0; page < l.pages; page++)
        printf(" %s=%d", map.page[page].key, l.banks[page]);
    printf("\nplacements %d headings %d\n", l.placements, l.headings);
    printf("desk_layout ok\n");
    return 0;
}
