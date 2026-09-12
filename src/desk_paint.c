#include "desk_paint.h"

#include <stdio.h>
#include <string.h>

#include "canvas_blend.h"
#include "desk_caption.h"
#include "desk_layout.h"
#include "desk_view.h"

// Alpha stays 0xFF everywhere, as in rescue_paint: the framebuffer is added as
// XRGB8888 and a zero top byte has already once turned the whole panel into a
// washed-out ghost.
#define BORDER 3
#define SWATCH_D 32

struct rect { int x, y, w, h; };

static struct rect of(const struct desk_placement *p) {
    struct rect r = { p->x, p->y, p->w, p->h };
    return r;
}

static int radius_for(enum desk_tile tile) {
    switch (tile) {
    case TILE_MASTER: return DESK_RADIUS;
    case TILE_COMPACT: return 12;
    default: return 20;
    }
}

static void tile_base(struct canvas *c, struct rect r, int radius, uint32_t fill) {
    canvas_round_rect(c, r.x, r.y, r.w, r.h, radius, fill);
}

// A pressed tile gets an ink outline and nothing else. The fill is the show's
// to give: the tile lights when the master says the function runs, and a
// finger must not paint amber, since amber means on. The interior is put
// back in whatever fill the tile already had.
static void press_outline(struct canvas *c, struct rect r, int radius, uint32_t fill) {
    canvas_round_rect(c, r.x, r.y, r.w, r.h, radius, DESK_INK);
    canvas_round_rect(c, r.x + BORDER, r.y + BORDER, r.w - 2 * BORDER, r.h - 2 * BORDER,
                      radius - BORDER, fill);
}

static void centred(struct canvas *c, struct font *f, int x, int y, int w,
                    const char *text, uint32_t col) {
    if (!f) {
        int scale = 3;
        int width = canvas_text_width(text, scale);
        canvas_text(c, x + (w - width) / 2, y, text, scale, col);
        return;
    }
    int width = font_width(f, text);
    if (width > w)
        width = w;
    font_draw_fit(f, c, x + (w - width) / 2, y + font_baseline(f), w, text, col);
}

static int line_pitch(struct font *f) { return f ? font_height(f) + 4 : 20; }

// The caption on one or two measured lines, centred on `mid_y`.
static void caption(struct canvas *c, struct font *f, int x, int w, int mid_y,
                    const char *text, uint32_t col) {
    struct desk_caption_lines lines;
    desk_caption_fit(f, w, text, &lines);
    int pitch = line_pitch(f);
    int top = mid_y - (lines.lines * pitch) / 2;
    for (int i = 0; i < lines.lines; i++)
        centred(c, f, x, top + i * pitch, w, lines.line[i], col);
}

static uint32_t swatch_or(const struct desk_control *ctl, int i, uint32_t fallback) {
    return i < ctl->swatches ? ctl->swatch[i] : fallback;
}

static void paint_cue(struct canvas *c, const struct desk_control *ctl,
                      const struct desk_placement *p, const struct desk_fonts *fonts,
                      enum desk_link link) {
    struct rect r = of(p);
    int radius = radius_for(p->tile);
    uint32_t fill = DESK_TILE;
    uint32_t ink = DESK_INK;
    if (ctl->state == DESK_ON) {
        fill = DESK_AMBER;
        ink = DESK_GLASS;
    }
    tile_base(c, r, radius, fill);
    if (ctl->pressed)
        press_outline(c, r, radius, fill);
    if (!ctl->enabled || link != DESK_LINK_READY)
        ink = DESK_MUTED;

    struct desk_caption_lines lines;
    desk_caption_fit(fonts->tile, r.w - 24, ctl->label, &lines);
    const char *under = NULL;
    if (!ctl->enabled && ctl->reason[0])
        under = ctl->reason;
    else if (ctl->state == DESK_UNKNOWN && ctl->enabled)
        under = "unknown";
    else if (ctl->enabled && ctl->detail[0])
        under = ctl->detail;
    if (lines.lines <= 1) {
        centred(c, fonts->tile, r.x + 12, r.y + r.h / 2 - 22, r.w - 24, ctl->label, ink);
        if (under)
            centred(c, fonts->label, r.x + 12, r.y + r.h / 2 + 6, r.w - 24, under,
                    ctl->state == DESK_ON && ctl->enabled ? DESK_GLASS : DESK_MUTED);
    } else {
        caption(c, fonts->tile, r.x + 12, r.w - 24, r.y + r.h / 2, ctl->label, ink);
    }
}

// A pick: a swatch ring over a small caption. The ring is the look's own
// colour; it fills when the master says the pick runs.
static void paint_swatch(struct canvas *c, const struct desk_control *ctl,
                         const struct desk_placement *p, const struct desk_fonts *fonts,
                         enum desk_link link) {
    struct rect r = of(p);
    int radius = radius_for(p->tile);
    int on = ctl->state == DESK_ON;
    uint32_t fill = on ? DESK_AMBER : DESK_TILE;
    tile_base(c, r, radius, fill);
    if (ctl->pressed)
        press_outline(c, r, radius, fill);
    int cx = r.x + r.w / 2, cy = r.y + 8;
    uint32_t first = swatch_or(ctl, 0, DESK_MUTED), second = swatch_or(ctl, 1, first);
    canvas_round_rect(c, cx - SWATCH_D / 2, cy, SWATCH_D, SWATCH_D, SWATCH_D / 2, first);
    if (ctl->swatches > 1)
        canvas_round_rect(c, cx, cy, SWATCH_D / 2, SWATCH_D, 0, second);
    if (!on) {
        // A ring: the hole shows the tile through it.
        int hole = SWATCH_D - 12;
        canvas_round_rect(c, cx - hole / 2, cy + 6, hole, hole, hole / 2, fill);
    }
    // Black and near-black swatches need a keyline to exist on the tile.
    if (ctl->swatches && (first & 0xFFFFFF) < 0x202020)
        canvas_round_rect(c, cx - SWATCH_D / 2 - 1, cy - 1, 2, SWATCH_D + 2, 1, DESK_MUTED);
    uint32_t ink = on ? DESK_GLASS : DESK_INK;
    if (!ctl->enabled || link != DESK_LINK_READY)
        ink = DESK_MUTED;
    caption(c, fonts->small, r.x + 4, r.w - 8, r.y + SWATCH_D + 8 + (r.h - SWATCH_D - 8) / 2,
            ctl->label, ink);
}

static void paint_compact(struct canvas *c, const struct desk_control *ctl,
                          const struct desk_placement *p, const struct desk_fonts *fonts,
                          enum desk_link link) {
    struct rect r = of(p);
    int radius = radius_for(p->tile);
    int on = ctl->state == DESK_ON;
    uint32_t fill = on ? DESK_AMBER : DESK_TILE;
    tile_base(c, r, radius, fill);
    if (ctl->pressed)
        press_outline(c, r, radius, fill);
    uint32_t ink = on ? DESK_GLASS : DESK_INK;
    if (!ctl->enabled || link != DESK_LINK_READY)
        ink = DESK_MUTED;
    caption(c, fonts->small, r.x + 4, r.w - 8, r.y + r.h / 2, ctl->label, ink);
}

static void paint_master(struct canvas *c, const struct desk_control *ctl,
                         const struct desk_placement *p, const struct desk_fonts *fonts,
                         enum desk_link link) {
    struct rect r = of(p);
    tile_base(c, r, DESK_RADIUS, DESK_TILE);
    // The fill is the master's word and nothing else: unknown draws no fill,
    // and with no link the last level heard is drawn grey so nobody reads a
    // number off a dead connection.
    int live = link == DESK_LINK_READY;
    int known = ctl->state != DESK_UNKNOWN;
    uint32_t fill_colour = live ? DESK_AMBER : DESK_MUTED;
    int fill_h = known ? ctl->level * r.h / 255 : 0;
    if (fill_h > 0) {
        canvas_round_rect(c, r.x, r.y + r.h - fill_h, r.w, fill_h,
                          fill_h < DESK_RADIUS ? fill_h / 2 : DESK_RADIUS, fill_colour);
    }
    // A finger on the fader gets a thumb at the level it is asking for; the
    // fill waits for the master to agree.
    if (ctl->pressed) {
        int span = r.h > 1 ? r.h - 1 : 1;
        int thumb_y = r.y + r.h - 1 - ctl->requested_level * span / 255;
        if (thumb_y > r.y + r.h - BORDER)
            thumb_y = r.y + r.h - BORDER;
        canvas_fill_rect(c, r.x, thumb_y, r.w, BORDER, DESK_INK);
    }
    char text[16];
    if (live && known)
        snprintf(text, sizeof text, "%d", ctl->level * 100 / 255);
    else
        snprintf(text, sizeof text, "--");
    int dark_text = known && ctl->level > 140;
    centred(c, fonts->value, r.x, r.y + r.h / 2 - 28, r.w, text, dark_text ? DESK_GLASS : DESK_INK);
    centred(c, fonts->label, r.x, r.y + 16, r.w, ctl->label,
            known && ctl->level > 220 ? DESK_GLASS : DESK_MUTED);
}

// The one red on the desk: the console's own StopAll, drawn as a warning
// rather than as a cue because it has no state to light and never will.
static void paint_stop_all(struct canvas *c, const struct desk_control *ctl,
                           const struct desk_placement *p, const struct desk_fonts *fonts,
                           enum desk_link link) {
    struct rect r = of(p);
    int live = ctl->enabled && link == DESK_LINK_READY;
    uint32_t fill = live ? DESK_WARN : DESK_TILE;
    tile_base(c, r, DESK_RADIUS, fill);
    if (ctl->pressed)
        press_outline(c, r, DESK_RADIUS, fill);
    centred(c, fonts->tile, r.x + 8, r.y + 18, r.w - 16, ctl->label, live ? DESK_INK : DESK_MUTED);
    const char *under = !ctl->enabled && ctl->reason[0] ? ctl->reason : ctl->detail;
    if (under[0])
        centred(c, fonts->label, r.x + 8, r.y + r.h - 34, r.w - 16, under,
                live ? DESK_INK : DESK_MUTED);
}

static const char *link_words(enum desk_link link) {
    switch (link) {
    case DESK_LINK_READY:      return "Linked";
    case DESK_LINK_SYNCING:    return "Reading";
    case DESK_LINK_CONNECTING: return "Connecting";
    default:                   return "No link";
    }
}

// The rail: one entry per page, the current one marked in ink (amber means
// on, and a page is not on), the lock target, and the link word.
static void paint_rail(struct canvas *c, const struct desk_model *model,
                       const struct desk_fonts *fonts) {
    canvas_fill_rect(c, 0, DESK_BAR_H, DESK_RAIL_W, DESK_H - DESK_BAR_H, DESK_TILE);
    for (int i = 0; i < model->layout.pages; i++) {
        struct desk_rect e = desk_view_rail_entry(i);
        int current = i == model->page;
        if (current)
            canvas_round_rect(c, 8, e.y + 10, 4, e.h - 20, 2, DESK_INK);
        if (fonts->label)
            font_draw_fit(fonts->label, c, 24, e.y + (e.h - font_height(fonts->label)) / 2 +
                          font_baseline(fonts->label), DESK_RAIL_W - 32,
                          model->layout.title[i], current ? DESK_INK : DESK_MUTED);
        else
            canvas_text(c, 24, e.y + 14, model->layout.title[i], 3,
                        current ? DESK_INK : DESK_MUTED);
    }
    struct desk_rect lock = desk_view_lock_target();
    // The ring reads amber while locked: the one state the rail carries.
    canvas_round_rect(c, lock.x, lock.y, lock.w, lock.h, lock.w / 2,
                      model->locked ? DESK_AMBER : DESK_MUTED);
    canvas_round_rect(c, lock.x + 3, lock.y + 3, lock.w - 6, lock.h - 6, lock.w / 2 - 3, DESK_TILE);
    // A padlock: the shackle and the body, in muted.
    canvas_round_rect(c, lock.x + 26, lock.y + 16, 20, 18, 10, DESK_MUTED);
    canvas_round_rect(c, lock.x + 30, lock.y + 20, 12, 14, 6, DESK_TILE);
    canvas_round_rect(c, lock.x + 22, lock.y + 32, 28, 22, 4, DESK_MUTED);

    const char *state = link_words(model->link);
    if (fonts->label)
        font_draw_fit(fonts->label, c, 20, DESK_H - 46 + font_baseline(fonts->label),
                      DESK_RAIL_W - 32, state,
                      model->link == DESK_LINK_READY ? DESK_MUTED : DESK_WARN);
}

static void paint_headings(struct canvas *c, const struct desk_model *model,
                           const struct desk_fonts *fonts) {
    for (int i = 0; i < model->layout.headings; i++) {
        const struct desk_heading *h = &model->layout.heading[i];
        if (h->page != model->page || h->bank != model->bank)
            continue;
        char text[64];
        if (h->parts > 1)
            snprintf(text, sizeof text, "%s  %d/%d", h->text, h->part, h->parts);
        else
            snprintf(text, sizeof text, "%s", h->text);
        if (fonts->label)
            font_draw_fit(fonts->label, c, h->x, h->y + font_baseline(fonts->label), h->w, text,
                          DESK_MUTED);
        else
            canvas_text(c, h->x, h->y, text, 3, DESK_MUTED);
    }
}

// The bank pills, only when the page has more than one bank.
static void paint_banks(struct canvas *c, const struct desk_model *model,
                        const struct desk_fonts *fonts) {
    int banks = model->layout.pages > 0 ? model->layout.banks[model->page] : 1;
    if (banks <= 1)
        return;
    for (int i = 0; i < banks; i++) {
        struct desk_rect b = desk_view_bank_button(i);
        int current = i == model->bank;
        canvas_round_rect(c, b.x, b.y, b.w, b.h, 12, current ? DESK_INK : DESK_TILE);
        char digit[16];
        snprintf(digit, sizeof digit, "%d", i + 1);
        centred(c, fonts->label, b.x, b.y + (b.h - (fonts->label ? font_height(fonts->label) : 15)) / 2,
                b.w, digit, current ? DESK_GLASS : DESK_MUTED);
    }
}

// When the link is down the whole desk says so across the grid, and no colour
// on the screen can be read as a running cue.
static void paint_link_banner(struct canvas *c, const struct desk_model *model,
                              const struct desk_fonts *fonts) {
    if (model->link == DESK_LINK_READY)
        return;
    int x = DESK_GRID_X, w = DESK_MASTER_X - DESK_GRID_X;
    canvas_round_rect(c, x, DESK_BAR_H + 4, w, 40, 12, DESK_WARN);
    char line[96];
    snprintf(line, sizeof line, "%s to the master - the Mac still has control",
             link_words(model->link));
    centred(c, fonts->label, x, DESK_BAR_H + 12, w, line, DESK_INK);
}

void desk_paint(struct canvas *canvas, const struct desk_model *model,
                const struct desk_fonts *fonts) {
    // The background, inside the clip only: a full clear is 614k pixels,
    // which is most of a frame's cost on this CPU when one tile changed.
    canvas_fill_rect(canvas, 0, 0, canvas->w, canvas->h, DESK_GLASS);

    paint_rail(canvas, model, fonts);
    paint_headings(canvas, model, fonts);

    for (int i = 0; i < model->layout.placements; i++) {
        const struct desk_placement *p = &model->layout.placement[i];
        if (p->page != model->page || p->bank != model->bank)
            continue;
        if (p->control < 0 || p->control >= model->count)
            continue;
        const struct desk_control *ctl = &model->control[p->control];
        switch (p->tile) {
        case TILE_MASTER:  paint_master(canvas, ctl, p, fonts, model->link); break;
        case TILE_PANIC:   paint_stop_all(canvas, ctl, p, fonts, model->link); break;
        case TILE_SWATCH:  paint_swatch(canvas, ctl, p, fonts, model->link); break;
        case TILE_COMPACT: paint_compact(canvas, ctl, p, fonts, model->link); break;
        default:           paint_cue(canvas, ctl, p, fonts, model->link); break;
        }
    }
    paint_banks(canvas, model, fonts);

    // Last, so it covers the tiles it is about rather than hiding behind them.
    paint_link_banner(canvas, model, fonts);
    if (model->locked) {
        int x = DESK_GRID_X, w = DESK_MASTER_X - DESK_GRID_X;
        canvas_round_rect(canvas, x, DESK_H - 100, w, 40, 12, DESK_AMBER);
        centred(canvas, fonts->label, x, DESK_H - 92, w,
                "Surface locked - hold the padlock to unlock", DESK_GLASS);
    }
}
