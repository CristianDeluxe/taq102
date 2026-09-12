#include "desk_paint.h"

#include <stdio.h>
#include <string.h>

#include "canvas_blend.h"
#include "desk_layout.h"

// Alpha stays 0xFF everywhere, as in rescue_paint: the framebuffer is added as
// XRGB8888 and a zero top byte has already once turned the whole panel into a
// washed-out ghost.
#define BORDER 3

static void tile_base(struct canvas *c, const struct desk_control *ctl,
                      uint32_t fill) {
    canvas_round_rect(c, ctl->x, ctl->y, ctl->w, ctl->h, DESK_RADIUS, fill);
}

// A pressed tile gets an ink outline and nothing else. The fill is the show's
// to give: the tile lights when the master says the function runs, and a
// finger must not paint amber, since amber means on. The interior is put
// back in whatever fill the tile already had.
static void press_outline(struct canvas *c, const struct desk_control *ctl,
                          uint32_t fill) {
    canvas_round_rect(c, ctl->x, ctl->y, ctl->w, ctl->h, DESK_RADIUS, DESK_INK);
    canvas_round_rect(c, ctl->x + BORDER, ctl->y + BORDER, ctl->w - 2 * BORDER,
                      ctl->h - 2 * BORDER, DESK_RADIUS - BORDER, fill);
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

static void paint_cue(struct canvas *c, const struct desk_control *ctl,
                      const struct desk_fonts *fonts, enum desk_link link) {
    uint32_t fill = DESK_TILE;
    uint32_t ink = DESK_INK;
    if (ctl->state == DESK_ON) {
        fill = DESK_AMBER;
        ink = DESK_GLASS;
    }
    tile_base(c, ctl, fill);
    if (ctl->pressed)
        press_outline(c, ctl, fill);

    int text_y = ctl->y + ctl->h / 2 - 16;
    if (!ctl->enabled || link != DESK_LINK_READY)
        ink = DESK_MUTED;
    centred(c, fonts->tile, ctl->x + 12, text_y, ctl->w - 24, ctl->label, ink);

    // An unknown state says so rather than looking off; a known one shows
    // the map's second line, when there is one.
    if (ctl->state == DESK_UNKNOWN && ctl->enabled)
        centred(c, fonts->label, ctl->x + 12, ctl->y + ctl->h - 44,
                ctl->w - 24, "unknown", DESK_MUTED);
    else if (ctl->enabled && ctl->detail[0])
        centred(c, fonts->label, ctl->x + 12, ctl->y + ctl->h - 44,
                ctl->w - 24, ctl->detail,
                ctl->state == DESK_ON ? DESK_GLASS : DESK_MUTED);
    if (!ctl->enabled && ctl->reason[0])
        centred(c, fonts->label, ctl->x + 12, ctl->y + ctl->h - 44,
                ctl->w - 24, ctl->reason, DESK_MUTED);
}

static void paint_master(struct canvas *c, const struct desk_control *ctl,
                         const struct desk_fonts *fonts, enum desk_link link) {
    tile_base(c, ctl, DESK_TILE);
    // The fill is the master's word and nothing else: unknown draws no fill,
    // and with no link the last level heard is drawn grey so nobody reads a
    // number off a dead connection.
    int live = link == DESK_LINK_READY;
    int known = ctl->state != DESK_UNKNOWN;
    uint32_t fill_colour = live ? DESK_AMBER : DESK_MUTED;
    int fill_h = known ? ctl->level * ctl->h / 255 : 0;
    if (fill_h > 0) {
        // The fill is a rounded rectangle clipped to the tile's own corners by
        // being drawn inside it: at full height it is the tile.
        canvas_round_rect(c, ctl->x, ctl->y + ctl->h - fill_h, ctl->w, fill_h,
                          fill_h < DESK_RADIUS ? fill_h / 2 : DESK_RADIUS,
                          fill_colour);
    }
    // A finger on the fader gets a thumb at the level it is asking for; the
    // fill waits for the master to agree.
    if (ctl->pressed) {
        int span = ctl->h > 1 ? ctl->h - 1 : 1;
        int thumb_y = ctl->y + ctl->h - 1 - ctl->requested_level * span / 255;
        if (thumb_y > ctl->y + ctl->h - BORDER)
            thumb_y = ctl->y + ctl->h - BORDER;
        canvas_fill_rect(c, ctl->x, thumb_y, ctl->w, BORDER, DESK_INK);
    }
    char text[16];
    if (live && known)
        snprintf(text, sizeof text, "%d", ctl->level * 100 / 255);
    else
        snprintf(text, sizeof text, "--");
    int dark_text = known && ctl->level > 140;
    centred(c, fonts->value, ctl->x, ctl->y + ctl->h / 2 - 28, ctl->w, text,
            dark_text ? DESK_GLASS : DESK_INK);
    centred(c, fonts->label, ctl->x, ctl->y + 16, ctl->w, ctl->label,
            known && ctl->level > 220 ? DESK_GLASS : DESK_MUTED);
}

// The one red on the desk: the console's own StopAll, drawn as a warning
// rather than as a cue because it has no state to light and never will.
static void paint_stop_all(struct canvas *c, const struct desk_control *ctl,
                           const struct desk_fonts *fonts, enum desk_link link) {
    int live = ctl->enabled && link == DESK_LINK_READY;
    tile_base(c, ctl, live ? DESK_WARN : DESK_TILE);
    if (ctl->pressed)
        press_outline(c, ctl, live ? DESK_WARN : DESK_TILE);
    centred(c, fonts->tile, ctl->x + 8, ctl->y + 18, ctl->w - 16, ctl->label,
            live ? DESK_INK : DESK_MUTED);
    const char *under = !ctl->enabled && ctl->reason[0] ? ctl->reason : ctl->detail;
    if (under[0])
        centred(c, fonts->label, ctl->x + 8, ctl->y + ctl->h - 34, ctl->w - 16,
                under, live ? DESK_INK : DESK_MUTED);
}

static const char *link_words(enum desk_link link) {
    switch (link) {
    case DESK_LINK_READY:      return "Linked";
    case DESK_LINK_SYNCING:    return "Reading";
    case DESK_LINK_CONNECTING: return "Connecting";
    default:                   return "No link";
    }
}

static void paint_rail(struct canvas *c, const struct desk_model *model,
                       const struct desk_fonts *fonts) {
    canvas_fill_rect(c, 0, DESK_BAR_H, DESK_RAIL_W, DESK_H - DESK_BAR_H,
                     DESK_TILE);
    uint32_t dot = model->link == DESK_LINK_READY ? DESK_AMBER : DESK_WARN;
    canvas_round_rect(c, 20, DESK_BAR_H + 24, 14, 14, 7, dot);
    if (fonts->label) {
        font_draw_fit(fonts->label, c, 44, DESK_BAR_H + 24 + font_baseline(fonts->label),
                      DESK_RAIL_W - 56, "LIVE", DESK_INK);
    }
    const char *state = link_words(model->link);
    if (fonts->label) {
        font_draw_fit(fonts->label, c, 20,
                      DESK_H - 46 + font_baseline(fonts->label),
                      DESK_RAIL_W - 32, state,
                      model->link == DESK_LINK_READY ? DESK_MUTED : DESK_WARN);
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
    for (int i = 0; i < canvas->w * canvas->h; i++)
        canvas->px[i] = DESK_GLASS;

    paint_rail(canvas, model, fonts);

    for (int i = 0; i < model->count; i++) {
        const struct desk_control *ctl = &model->control[i];
        if (ctl->w <= 0 || ctl->h <= 0)
            continue;               // validated, kept, not on this screen
        switch (ctl->kind) {
        case DESK_CUE:      paint_cue(canvas, ctl, fonts, model->link); break;
        case DESK_MASTER:   paint_master(canvas, ctl, fonts, model->link); break;
        case DESK_STOP_ALL: paint_stop_all(canvas, ctl, fonts, model->link); break;
        }
    }

    // Last, so it covers the tiles it is about rather than hiding behind them.
    paint_link_banner(canvas, model, fonts);
}
