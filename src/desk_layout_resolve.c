#include "desk_layout_resolve.h"

#include <stdio.h>
#include <string.h>

#include "desk_layout.h"
#include "desk_show_layout.h"

// The content: 16..844 across, 56..584 down. A section is a titled rule
// (24 px) with 8 px under it, then rows of tiles with 8 px gaps and 16 px
// after the section. A page that does not fit spills into the next bank,
// which the pager reaches; the pager takes the bottom 40 px of every bank
// of such a page.
#define CONTENT_X DESK_CONTENT_X
#define CONTENT_W DESK_CONTENT_W
#define CONTENT_Y DESK_CONTENT_Y
#define CONTENT_END DESK_CONTENT_END
#define PAGER_STRIP 56
#define HEADING_H 24
#define HEADING_GAP 8
#define SECTION_GAP 16

struct tile_size { int w, h; };

static struct tile_size size_of(enum desk_tile tile) {
    switch (tile) {
    case TILE_WIDE:    return (struct tile_size){ DESK_HOOK_W, DESK_HOOK_H };
    case TILE_SWATCH:  return (struct tile_size){ DESK_PICK_W, DESK_PICK_H };
    case TILE_HOLD:    return (struct tile_size){ DESK_PICK_W, DESK_HOLD_TILE_H };
    case TILE_HAZE:    return (struct tile_size){ DESK_HOOK_W, DESK_HOOK_H };
    case TILE_COMPACT: return (struct tile_size){ DESK_PICK_W, DESK_HOOK_H };
    default:           return (struct tile_size){ DESK_HOOK_W, DESK_HOOK_H };
    }
}

// The tile a control gets from its role: states, hooks, toggles and chases
// are 200x64 buttons; picks 128x88 with their colour; a hit held on the Mac
// or fired from here 128x104 in the hold look.
static enum desk_tile tile_for(const struct map_control *c) {
    if (c->role == MAP_ROLE_ACCENT)
        return TILE_HOLD;
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
    if (cur->out->placements >= DESK_MAX_PLACEMENTS) {
        cur->failed = 1;
        return;
    }
    struct tile_size s = size_of(tile);
    struct desk_placement *p = &cur->out->placement[cur->out->placements++];
    p->control = control;
    p->page = cur->page;
    p->bank = cur->bank;
    p->x = x;
    p->y = y;
    p->w = s.w;
    p->h = s.h;
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

static void heading(struct cursor *cur, const char *text, int part, int parts, int section) {
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
    h->section = section;
    h->part = part;
    h->parts = parts;
    cur->y += HEADING_H + HEADING_GAP;
}

static int row_fits(int x, int w) { return x + w <= CONTENT_X + CONTENT_W; }

// Lays one section's controls from `from` on, as many rows as fit the bank,
// and returns the index of the first control not placed.
static int lay_rows(struct cursor *cur, const struct show_map *map,
                    const struct map_section *sec, int from) {
    int i = from;
    while (i < sec->first + sec->count) {
        struct tile_size sz = size_of(tile_for(&map->control[i]));
        if (cur->y + sz.h > cur->bank_end)
            return i;
        int x = CONTENT_X, row_h = sz.h;
        while (i < sec->first + sec->count) {
            enum desk_tile t = tile_for(&map->control[i]);
            struct tile_size s = size_of(t);
            if (!row_fits(x, s.w))
                break;
            place(cur, i, x, cur->y, t);
            x += s.w + DESK_GAP;
            if (s.h > row_h)
                row_h = s.h;
            i++;
        }
        cur->y += row_h + DESK_GAP;
    }
    cur->y += SECTION_GAP - DESK_GAP;
    return i;
}

static int section_height(const struct show_map *map, const struct map_section *sec) {
    int y = HEADING_H + HEADING_GAP;
    int i = sec->first;
    while (i < sec->first + sec->count) {
        int x = CONTENT_X, row_h = 0;
        while (i < sec->first + sec->count) {
            struct tile_size s = size_of(tile_for(&map->control[i]));
            if (!row_fits(x, s.w))
                break;
            x += s.w + DESK_GAP;
            if (s.h > row_h)
                row_h = s.h;
            i++;
        }
        y += row_h + DESK_GAP;
    }
    return y - DESK_GAP;
}

static void lay_page(struct cursor *cur, const struct show_map *map, int page,
                     int master, int panic, int bank_end) {
    cur->page = page;
    cur->bank = 0;
    cur->y = CONTENT_Y;
    cur->bank_end = bank_end;
    chrome(cur, master, panic);
    const struct map_page *p = &map->page[page];
    for (int s = 0; s < p->sections; s++) {
        const struct map_section *sec = &p->section[s];
        // A section that does not fit what is left moves whole to the next
        // bank, unless at least two of its rows still fit here.
        int need = section_height(map, sec);
        int row_h = size_of(tile_for(&map->control[sec->first])).h;
        int two_rows = HEADING_H + HEADING_GAP + 2 * row_h + DESK_GAP;
        if (cur->y + need > cur->bank_end && cur->y > CONTENT_Y &&
            cur->y + two_rows > cur->bank_end)
            next_bank(cur, master, panic);
        int part = 1, parts = 1;
        int i = sec->first;
        while (i < sec->first + sec->count && !cur->failed) {
            heading(cur, sec->title, part, parts, s);
            int next = lay_rows(cur, map, sec, i);
            if (next == i) {
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

int desk_layout_resolve(const struct show_map *map, int master, int panic, int haze_off,
                        int tempo, struct desk_layout *out) {
    memset(out, 0, sizeof *out);
    out->pages = map->pages;
    out->speed_page = -1;
    int live = desk_show_live_page(map);
    for (int page = 0; page < map->pages; page++)
        snprintf(out->title[page], sizeof out->title[page], "%s",
                 page == live ? "SHOW" : map->page[page].title);

    for (int page = 0; page < map->pages; page++) {
        if (page == live) {
            struct cursor cur = { out, page, 0, CONTENT_Y, CONTENT_END, 0 };
            chrome(&cur, master, panic);
            if (cur.failed || desk_show_compose(map, page, haze_off, tempo, out) != 0)
                return -1;
            out->banks[page] = 1;
            continue;
        }
        // Lay the page once against the full height; if it needs more than
        // one bank, the pager takes the bottom and it is laid again.
        struct cursor cur = { out, page, 0, CONTENT_Y, CONTENT_END, 0 };
        int placements_before = out->placements, headings_before = out->headings;
        lay_page(&cur, map, page, master, panic, CONTENT_END);
        if (cur.failed)
            return -1;
        if (out->banks[page] > 1) {
            out->placements = placements_before;
            out->headings = headings_before;
            cur = (struct cursor){ out, page, 0, CONTENT_Y, CONTENT_END - PAGER_STRIP, 0 };
            lay_page(&cur, map, page, master, panic, CONTENT_END - PAGER_STRIP);
            if (cur.failed)
                return -1;
        }
    }
    if (map->dials > 0 && map->pages < MAP_MAX_PAGES) {
        int page = map->pages;
        struct cursor cur = { out, page, 0, CONTENT_Y, CONTENT_END, 0 };
        chrome(&cur, master, panic);
        if (cur.failed)
            return -1;
        out->banks[page] = 1;
        snprintf(out->title[page], sizeof out->title[page], "SPEED");
        out->speed_page = page;
        out->pages = page + 1;
    }
    return 0;
}
