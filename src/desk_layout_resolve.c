#include "desk_layout_resolve.h"

#include <stdio.h>
#include <string.h>

#include "desk_layout.h"

#define CONTENT_X DESK_GRID_X                    // 156
#define CONTENT_W (DESK_MASTER_X - DESK_GRID_X)  // 688
#define CONTENT_Y DESK_GRID_Y                    // 64
#define CONTENT_END DESK_H                       // 584
#define SELECTOR_H 56
#define HEADING_H 24
#define HEADING_GAP 8
#define SECTION_GAP 16
#define ROW_GAP 16
#define COMPACT_GAP 8

struct tile_size { int w, h, per_row, gap; };

static struct tile_size size_of(enum desk_tile tile) {
    switch (tile) {
    case TILE_WIDE:    return (struct tile_size){ 452, 88, 1, ROW_GAP };
    case TILE_SWATCH:  return (struct tile_size){ 100, 88, 6, ROW_GAP };
    case TILE_COMPACT: return (struct tile_size){ 86, 56, 7, COMPACT_GAP };
    case TILE_HAZE:    return (struct tile_size){ 160, 88, 4, ROW_GAP };
    default:           return (struct tile_size){ 218, 88, 3, ROW_GAP };
    }
}

static enum desk_tile tile_for(const struct map_control *c, int first_of_state) {
    if (c->role == MAP_ROLE_STATE)
        return first_of_state ? TILE_WIDE : TILE_CUE;
    if (c->role == MAP_ROLE_PICK)
        return TILE_SWATCH;
    if (c->role == MAP_ROLE_HAZE)
        return TILE_HAZE;
    return TILE_CUE;
}

struct cursor {
    struct desk_layout *out;
    int page, bank, y, bank_end;
    int failed;
};

static void place(struct cursor *cur, int control, int x, int y, enum desk_tile tile) {
    struct tile_size sz = size_of(tile);
    if (cur->out->placements >= DESK_MAX_PLACEMENTS) {
        cur->failed = 1;
        return;
    }
    struct desk_placement *p = &cur->out->placement[cur->out->placements++];
    p->control = control;
    p->page = cur->page;
    p->bank = cur->bank;
    p->x = x;
    p->y = y;
    p->w = sz.w;
    p->h = sz.h;
    p->tile = tile;
}

static void chrome(struct cursor *cur, int master, int panic) {
    if (cur->out->placements + 2 > DESK_MAX_PLACEMENTS) {
        cur->failed = 1;
        return;
    }
    struct desk_placement *m = &cur->out->placement[cur->out->placements++];
    *m = (struct desk_placement){ master, cur->page, cur->bank, DESK_MASTER_TILE_X,
                                  DESK_MASTER_TILE_Y, DESK_MASTER_TILE_W, DESK_MASTER_TILE_H,
                                  TILE_MASTER };
    struct desk_placement *p = &cur->out->placement[cur->out->placements++];
    *p = (struct desk_placement){ panic, cur->page, cur->bank, DESK_MASTER_TILE_X,
                                  DESK_PANIC_Y, DESK_MASTER_TILE_W, DESK_PANIC_H, TILE_PANIC };
}

static void next_bank(struct cursor *cur, int master, int panic) {
    cur->bank++;
    cur->y = CONTENT_Y;
    if (cur->bank >= DESK_MAX_BANKS) {
        cur->failed = 1;
        return;
    }
    chrome(cur, master, panic);
}

static void heading(struct cursor *cur, const char *text, int part, int parts) {
    if (cur->out->headings >= DESK_MAX_HEADINGS) {
        cur->failed = 1;
        return;
    }
    struct desk_heading *h = &cur->out->heading[cur->out->headings++];
    h->page = cur->page;
    h->bank = cur->bank;
    h->x = CONTENT_X;
    h->y = cur->y;
    h->w = CONTENT_W;
    snprintf(h->text, sizeof h->text, "%s", text);
    h->part = part;
    h->parts = parts;
    cur->y += HEADING_H + HEADING_GAP;
}

// The wide tile shares its row with one cue: the row is laid as widths, not
// as a count, so the first state row is AUTO plus CHARLA.
static int row_fits(int x, int w) { return x + w <= CONTENT_X + CONTENT_W; }

// Lays one section's controls from `from` on, as many rows as fit the bank,
// and returns the index of the first control not placed.
static int lay_rows(struct cursor *cur, const struct show_map *map,
                    const struct map_section *sec, int from, int is_state) {
    int i = from;
    while (i < sec->first + sec->count) {
        // One row.
        enum desk_tile t0 = tile_for(&map->control[i], is_state && i == sec->first);
        struct tile_size sz = size_of(t0);
        if (cur->y + sz.h > cur->bank_end)
            return i;
        int x = CONTENT_X;
        int row_h = sz.h;
        while (i < sec->first + sec->count) {
            enum desk_tile t = tile_for(&map->control[i], is_state && i == sec->first);
            struct tile_size s = size_of(t);
            if (!row_fits(x, s.w))
                break;
            place(cur, i, x, cur->y, t);
            x += s.w + s.gap;
            if (s.h > row_h)
                row_h = s.h;
            i++;
        }
        cur->y += row_h + ROW_GAP;
    }
    cur->y += SECTION_GAP - ROW_GAP;
    return i;
}

// The height a section needs whole, for deciding whether to start a bank.
static int section_height(const struct show_map *map, const struct map_section *sec, int is_state) {
    int y = HEADING_H + HEADING_GAP;
    int i = sec->first;
    while (i < sec->first + sec->count) {
        int x = CONTENT_X, row_h = 0;
        while (i < sec->first + sec->count) {
            struct tile_size s = size_of(tile_for(&map->control[i], is_state && i == sec->first));
            if (!row_fits(x, s.w))
                break;
            x += s.w + s.gap;
            if (s.h > row_h)
                row_h = s.h;
            i++;
        }
        y += row_h + ROW_GAP;
    }
    return y - ROW_GAP;
}

static void compact_row(struct cursor *cur, const struct map_section *state) {
    int x = CONTENT_X;
    for (int i = state->first; i < state->first + state->count; i++) {
        struct tile_size s = size_of(TILE_COMPACT);
        if (!row_fits(x, s.w))
            break;
        place(cur, i, x, cur->y, TILE_COMPACT);
        x += s.w + s.gap;
    }
    cur->y += size_of(TILE_COMPACT).h + SECTION_GAP;
}

static void lay_page(struct cursor *cur, const struct show_map *map, int page,
                     const struct map_section *state, int master, int panic, int bank_end) {
    cur->page = page;
    cur->bank = 0;
    cur->y = CONTENT_Y;
    cur->bank_end = bank_end;
    chrome(cur, master, panic);
    if (page != 0 && state)
        compact_row(cur, state);
    const struct map_page *p = &map->page[page];
    for (int s = 0; s < p->sections; s++) {
        const struct map_section *sec = &p->section[s];
        int is_state = page == 0 && s == 0 && strcmp(sec->key, "state") == 0;
        // A section that does not fit what is left of the bank moves whole
        // to the next one, unless at least two of its rows still fit here:
        // then it is split, so a family's picks start beside its hooks.
        int need = section_height(map, sec, is_state);
        int row_h = size_of(tile_for(&map->control[sec->first], 0)).h;
        int two_rows = HEADING_H + HEADING_GAP + 2 * row_h + ROW_GAP;
        if (cur->y + need > cur->bank_end && cur->y > CONTENT_Y &&
            cur->y + two_rows > cur->bank_end)
            next_bank(cur, master, panic);
        int part = 1, parts = 1;
        int i = sec->first;
        while (i < sec->first + sec->count && !cur->failed) {
            // How many parts this section will take is known only once it is
            // split; headings are patched afterwards.
            heading(cur, sec->title, part, parts);
            int next = lay_rows(cur, map, sec, i, is_state);
            if (next == i) {
                // Not even one row fit: a fresh bank.
                cur->out->headings--;
                next_bank(cur, master, panic);
                continue;
            }
            i = next;
            if (i < sec->first + sec->count) {
                part++;
                next_bank(cur, master, panic);
            }
        }
        // Patch part/parts on the headings of this section.
        if (part > 1) {
            int seen = 0;
            for (int h = cur->out->headings - 1; h >= 0 && seen < part; h--) {
                if (strcmp(cur->out->heading[h].text, sec->title) == 0 &&
                    cur->out->heading[h].page == page) {
                    cur->out->heading[h].parts = part;
                    cur->out->heading[h].part = part - seen;
                    seen++;
                }
            }
        }
    }
    cur->out->banks[page] = cur->bank + 1;
}

int desk_layout_resolve(const struct show_map *map, int master, int panic,
                        struct desk_layout *out) {
    memset(out, 0, sizeof *out);
    out->pages = map->pages;
    for (int page = 0; page < map->pages; page++)
        snprintf(out->title[page], sizeof out->title[page], "%s", map->page[page].title);
    const struct map_section *state = NULL;
    if (map->pages > 0 && map->page[0].sections > 0 &&
        strcmp(map->page[0].section[0].key, "state") == 0)
        state = &map->page[0].section[0];

    for (int page = 0; page < map->pages; page++) {
        // Lay the page once against the full height; if it needs more than
        // one bank, the selector strip takes the bottom and it is laid again.
        struct cursor cur = { out, page, 0, CONTENT_Y, CONTENT_END, 0 };
        int placements_before = out->placements, headings_before = out->headings;
        lay_page(&cur, map, page, state, master, panic, CONTENT_END);
        if (cur.failed)
            return -1;
        if (out->banks[page] > 1) {
            out->placements = placements_before;
            out->headings = headings_before;
            cur = (struct cursor){ out, page, 0, CONTENT_Y, CONTENT_END - SELECTOR_H, 0 };
            lay_page(&cur, map, page, state, master, panic, CONTENT_END - SELECTOR_H);
            if (cur.failed)
                return -1;
        }
    }
    return 0;
}
