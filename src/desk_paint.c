#include "desk_paint.h"

#include <stdio.h>
#include <string.h>

#include "canvas_blend.h"
#include "desk_caption.h"
#include "desk_layout.h"
#include "desk_view.h"
#include "icon.h"

// Alpha stays 0xFF everywhere, as in rescue_paint: the framebuffer is added
// as XRGB8888 and a zero top byte has already once turned the whole panel
// into a washed-out ghost.
#define BORDER 3
#define DISC 28

struct rect { int x, y, w, h; };

static struct rect of(const struct desk_placement *p) {
    struct rect r = { p->x, p->y, p->w, p->h };
    return r;
}

// ---- text helpers: every string is drawn with a font when there is one
// and with the 3x5 glyphs when there is not.

static int text_w(struct font *f, const char *s) {
    return f ? font_width(f, s) : canvas_text_width(s, 3);
}

static int text_h(struct font *f) { return f ? font_height(f) : 15; }

static void text_at(struct canvas *c, struct font *f, int x, int y, int max_w, const char *s,
                    uint32_t col) {
    if (f)
        font_draw_fit(f, c, x, y + font_baseline(f), max_w, s, col);
    else
        canvas_text(c, x, y, s, 3, col);
}

static void centred(struct canvas *c, struct font *f, int x, int y, int w, int h, const char *s,
                    uint32_t col) {
    int width = text_w(f, s);
    if (width > w)
        width = w;
    text_at(c, f, x + (w - width) / 2, y + (h - text_h(f)) / 2, w, s, col);
}

static void right(struct canvas *c, struct font *f, int right_x, int y, const char *s, uint32_t col) {
    int width = text_w(f, s);
    text_at(c, f, right_x - width, y, width + 1, s, col);
}

// The caption on one or two measured lines, centred on `mid_y`.
static void caption(struct canvas *c, struct font *f, int x, int w, int mid_y, const char *text,
                    uint32_t col) {
    struct desk_caption_lines lines;
    desk_caption_fit(f, w, text, &lines);
    int pitch = text_h(f) + 3;
    int top = mid_y - (lines.lines * pitch) / 2;
    for (int i = 0; i < lines.lines; i++)
        centred(c, f, x, top + i * pitch, w, pitch, lines.line[i], col);
}

static void upper(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++)
        dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char)(src[i] - 32) : src[i];
    dst[i] = '\0';
}

void desk_paint_section(struct canvas *c, const struct desk_fonts *fonts, int x, int y, int w,
                        const char *title) {
    char cap[96];
    upper(cap, sizeof cap, title);
    text_at(c, fonts->section, x, y + 4, w, cap, DESK_MUTED);
    int tw = text_w(fonts->section, cap);
    if (tw + 16 < w)
        canvas_fill_rect(c, x + tw + 12, y + 12, w - tw - 12, 1, DESK_LINE);
}

// ---- tiles

// A pressed tile gets an ink outline and nothing else. The fill is the
// show's to give: the tile lights when the master says the function runs,
// and a finger must not paint amber, since amber means on.
static void press_outline(struct canvas *c, struct rect r, uint32_t fill) {
    canvas_round_rect(c, r.x, r.y, r.w, r.h, DESK_RADIUS, DESK_INK);
    canvas_round_rect(c, r.x + BORDER, r.y + BORDER, r.w - 2 * BORDER, r.h - 2 * BORDER,
                      DESK_RADIUS - BORDER, fill);
}

// The words a tile carries under its name, one look across every tile: a
// disabled one says why in plain words, an unknown one says so, an idle one
// shows the show's own detail when there is room for it.
static const char *state_line(const struct desk_control *ctl, enum desk_link link) {
    if (!ctl->enabled) {
        if (strncmp(ctl->reason, "held", 4) == 0)
            return "solo en el Mac";
        return ctl->reason[0] ? ctl->reason : "no disponible";
    }
    if (link != DESK_LINK_READY)
        return "";
    if (ctl->state == DESK_UNKNOWN)
        return "?";
    return ctl->detail;
}

static uint32_t tile_fill(const struct desk_control *ctl) {
    if (!ctl->enabled)
        return DESK_GLASS;
    return ctl->state == DESK_ON ? DESK_AMBER : DESK_TILE;
}

static void pending_mark(struct canvas *c, const struct desk_control *ctl, struct rect r) {
    if (ctl->pending)
        canvas_round_rect(c, r.x + r.w - 18, r.y + 8, 10, 10, 5, DESK_INK);
}

// A 200x64 button: the name, and under it the one line that matters.
static void paint_cue(struct canvas *c, const struct desk_control *ctl,
                      const struct desk_placement *p, const struct desk_fonts *fonts,
                      enum desk_link link) {
    struct rect r = of(p);
    uint32_t fill = tile_fill(ctl);
    int on = ctl->state == DESK_ON && ctl->enabled;
    uint32_t ink = on ? DESK_AMBER_INK : DESK_INK;
    canvas_round_rect(c, r.x, r.y, r.w, r.h, DESK_RADIUS, fill);
    if (!ctl->enabled)
        canvas_round_rect(c, r.x, r.y, r.w, r.h, DESK_RADIUS, DESK_LINE),
        canvas_round_rect(c, r.x + 1, r.y + 1, r.w - 2, r.h - 2, DESK_RADIUS - 1, DESK_GLASS);
    if (ctl->pressed)
        press_outline(c, r, fill);
    if (!ctl->enabled || link != DESK_LINK_READY)
        ink = DESK_MUTED;
    const char *under = state_line(ctl, link);
    struct desk_caption_lines lines;
    desk_caption_fit(fonts->tile, r.w - 24, ctl->label, &lines);
    int keep_line = under[0] && (!ctl->enabled || ctl->state == DESK_UNKNOWN);
    if (under[0] && (lines.lines <= 1 || keep_line)) {
        const char *name = lines.lines <= 1 ? ctl->label : lines.line[0];
        centred(c, fonts->tile, r.x + 12, r.y + 10, r.w - 24, text_h(fonts->tile) + 4, name, ink);
        centred(c, fonts->small, r.x + 12, r.y + r.h - 12 - text_h(fonts->small) - 2, r.w - 24,
                text_h(fonts->small) + 4, under, on ? DESK_AMBER_INK : DESK_MUTED);
    } else {
        caption(c, fonts->tile, r.x + 12, r.w - 24, r.y + r.h / 2, ctl->label, ink);
    }
    pending_mark(c, ctl, r);
}

// A pick: its colours as a disc (segments for a mix) over its name, the
// whole tile the target. A pick with no colour has its name alone, larger.
static void paint_swatch(struct canvas *c, const struct desk_control *ctl,
                         const struct desk_placement *p, const struct desk_fonts *fonts,
                         enum desk_link link) {
    struct rect r = of(p);
    int on = ctl->state == DESK_ON && ctl->enabled;
    uint32_t fill = tile_fill(ctl);
    canvas_round_rect(c, r.x, r.y, r.w, r.h, DESK_RADIUS, fill);
    // Amber as a ring, so the pick's own colour stays visible when it runs.
    if (on) {
        canvas_round_rect(c, r.x, r.y, r.w, r.h, DESK_RADIUS, DESK_AMBER);
        canvas_round_rect(c, r.x + BORDER, r.y + BORDER, r.w - 2 * BORDER, r.h - 2 * BORDER,
                          DESK_RADIUS - BORDER, DESK_RAISED);
        fill = DESK_RAISED;
    }
    if (ctl->pressed)
        press_outline(c, r, fill);
    uint32_t ink = DESK_INK;
    if (!ctl->enabled || link != DESK_LINK_READY)
        ink = DESK_MUTED;
    pending_mark(c, ctl, r);
    if (ctl->swatches == 0) {
        caption(c, fonts->tile_s, r.x + 8, r.w - 16, r.y + r.h / 2, ctl->label, ink);
        return;
    }
    int cx = r.x + r.w / 2, cy = r.y + 10;
    int old_x = c->clip_x, old_y = c->clip_y, old_w = c->clip_w, old_h = c->clip_h;
    int n = ctl->swatches > DESK_MAX_SWATCHES ? DESK_MAX_SWATCHES : ctl->swatches;
    for (int i = 0; i < n; i++) {
        int sx = cx - DISC / 2 + i * DISC / n;
        int sw = (i + 1) * DISC / n - i * DISC / n;
        int cx0 = sx, cy0 = cy, cw = sw, ch = DISC;
        if (old_w > 0) {
            int x1 = cx0 > old_x ? cx0 : old_x, y1 = cy0 > old_y ? cy0 : old_y;
            int x2 = cx0 + cw < old_x + old_w ? cx0 + cw : old_x + old_w;
            int y2 = cy0 + ch < old_y + old_h ? cy0 + ch : old_y + old_h;
            cx0 = x1; cy0 = y1; cw = x2 > x1 ? x2 - x1 : 0; ch = y2 > y1 ? y2 - y1 : 0;
        }
        if (cw <= 0 || ch <= 0)
            continue;
        canvas_set_clip(c, cx0, cy0, cw, ch);
        canvas_round_rect(c, cx - DISC / 2, cy, DISC, DISC, DISC / 2, ctl->swatch[i]);
    }
    if (old_w > 0)
        canvas_set_clip(c, old_x, old_y, old_w, old_h);
    else
        canvas_clear_clip(c);
    // Black and near-black discs need a keyline to exist on the tile.
    uint32_t first = ctl->swatch[0];
    int brightest = (first >> 16 & 0xFF) > (first >> 8 & 0xFF) ? (first >> 16 & 0xFF) : (first >> 8 & 0xFF);
    if (brightest < (int)(first & 0xFF))
        brightest = first & 0xFF;
    if (brightest < 0x20)
        canvas_round_rect(c, cx - DISC / 2 - 1, cy - 1, DISC + 2, DISC + 2, DISC / 2 + 1, DESK_MUTED),
        canvas_round_rect(c, cx - DISC / 2, cy, DISC, DISC, DISC / 2, first);
    caption(c, fonts->tile_s, r.x + 8, r.w - 16, r.y + DISC + 12 + (r.h - DISC - 12) / 2, ctl->label,
            ink);
}

// A hit: the hold look. Held on the Mac it is dead and says so; fired from
// here (a later stage) it lights with an ink ring while the finger is down.
static void paint_hold(struct canvas *c, const struct desk_control *ctl,
                       const struct desk_placement *p, const struct desk_fonts *fonts,
                       enum desk_link link) {
    struct rect r = of(p);
    int live = ctl->enabled && link == DESK_LINK_READY;
    canvas_round_rect(c, r.x, r.y, r.w, r.h, DESK_RADIUS, live ? DESK_HOLD_LINE : DESK_LINE);
    canvas_round_rect(c, r.x + 1, r.y + 1, r.w - 2, r.h - 2, DESK_RADIUS - 1,
                      live ? DESK_HOLD : DESK_GLASS);
    if (ctl->pressed)
        press_outline(c, r, DESK_RAISED);
    icon_paint(c, ICON_BOLT, r.x + 8, r.y + 6, live ? DESK_HOLD_LINE : DESK_LINE);
    caption(c, fonts->tile_s, r.x + 8, r.w - 16, r.y + r.h / 2 - 2, ctl->label,
            live ? DESK_INK : DESK_MUTED);
    const char *foot = live ? "mientras pulses" : "solo en el Mac";
    centred(c, fonts->small, r.x + 8, r.y + r.h - 24, r.w - 16, 18, foot, DESK_MUTED);
}

static void paint_compact(struct canvas *c, const struct desk_control *ctl,
                          const struct desk_placement *p, const struct desk_fonts *fonts,
                          enum desk_link link) {
    struct rect r = of(p);
    int on = ctl->state == DESK_ON && ctl->enabled;
    uint32_t fill = tile_fill(ctl);
    canvas_round_rect(c, r.x, r.y, r.w, r.h, DESK_RADIUS, fill);
    if (ctl->pressed)
        press_outline(c, r, fill);
    uint32_t ink = on ? DESK_AMBER_INK : DESK_INK;
    if (!ctl->enabled || link != DESK_LINK_READY)
        ink = DESK_MUTED;
    caption(c, fonts->tile_s, r.x + 8, r.w - 16, r.y + r.h / 2, ctl->label, ink);
    pending_mark(c, ctl, r);
}

// The master: a dark track, the level as a restrained fill, an ink thumb
// where the level is, the readout on its own backing. Amber is not a level.
static void paint_master(struct canvas *c, const struct desk_control *ctl,
                         const struct desk_placement *p, const struct desk_fonts *fonts,
                         enum desk_link link) {
    struct rect r = of(p);
    canvas_round_rect(c, r.x, r.y, r.w, r.h, DESK_RADIUS, DESK_TILE);
    int live = link == DESK_LINK_READY;
    int known = ctl->state != DESK_UNKNOWN;
    int track_y = r.y + 44, track_h = r.h - 44 - 16;
    canvas_round_rect(c, r.x + 16, track_y, r.w - 32, track_h, 8, DESK_GLASS);
    if (known) {
        int fill_h = ctl->level * track_h / 255;
        if (fill_h > 0)
            canvas_round_rect(c, r.x + 16, track_y + track_h - fill_h, r.w - 32, fill_h, 8,
                              live ? DESK_RAISED : DESK_LINE);
        int thumb_y = track_y + track_h - fill_h - 3;
        if (thumb_y < track_y)
            thumb_y = track_y;
        canvas_round_rect(c, r.x + 12, thumb_y, r.w - 24, 6, 3, live ? DESK_INK : DESK_MUTED);
    }
    if (ctl->pressed) {
        int span = track_h > 1 ? track_h - 1 : 1;
        int thumb_y = track_y + track_h - 1 - ctl->requested_level * span / 255;
        canvas_fill_rect(c, r.x + 8, thumb_y, r.w - 16, BORDER, DESK_AMBER);
    }
    char text[16];
    if (live && known)
        snprintf(text, sizeof text, "%d%%", ctl->level * 100 / 255);
    else
        snprintf(text, sizeof text, "--");
    int bh = text_h(fonts->value) + 12;
    canvas_round_rect(c, r.x + 12, r.y + r.h / 2 - bh / 2, r.w - 24, bh, 10, 0xFF000000u);
    centred(c, fonts->value, r.x, r.y + r.h / 2 - bh / 2, r.w, bh, text, DESK_INK);
    centred(c, fonts->section, r.x, r.y + 12, r.w, 20, "MASTER", DESK_MUTED);
}

// The one red on the desk: the console's own StopAll, drawn as a warning
// rather than as a cue because it has no state to light and never will.
static void paint_stop_all(struct canvas *c, const struct desk_control *ctl,
                           const struct desk_placement *p, const struct desk_fonts *fonts,
                           enum desk_link link) {
    struct rect r = of(p);
    int live = ctl->enabled && link == DESK_LINK_READY;
    uint32_t fill = live ? DESK_WARN : DESK_TILE;
    canvas_round_rect(c, r.x, r.y, r.w, r.h, DESK_RADIUS, fill);
    if (ctl->pressed)
        press_outline(c, r, fill);
    uint32_t ink = live ? DESK_INK : DESK_MUTED;
    centred(c, fonts->tile, r.x, r.y + r.h / 2 - 34, r.w, 24, "PARAR", ink);
    centred(c, fonts->tile, r.x, r.y + r.h / 2 - 10, r.w, 24, "TODO", ink);
    const char *foot = live ? (ctl->detail[0] ? ctl->detail : "") : state_line(ctl, link);
    centred(c, fonts->small, r.x, r.y + r.h / 2 + 22, r.w, 18, foot, live ? 0xFFFFC8BEu : DESK_MUTED);
}

// ---- the bar

static const char *link_words(enum desk_link link) {
    switch (link) {
    case DESK_LINK_READY:      return "enlazado";
    case DESK_LINK_SYNCING:    return "leyendo el show";
    case DESK_LINK_CONNECTING: return "conectando";
    default:                   return "sin master";
    }
}

static void paint_bar(struct canvas *c, const struct desk_model *model,
                      const struct desk_fonts *fonts) {
    canvas_fill_rect(c, 0, 0, DESK_W, DESK_BAR_H, DESK_GLASS);
    for (int i = 0; i < model->layout.pages; i++) {
        struct desk_rect t = desk_view_tab(i);
        int current = i == model->page;
        if (current)
            canvas_round_rect(c, t.x, t.y, t.w, t.h, 8, DESK_RAISED);
        char cap[48];
        upper(cap, sizeof cap, model->layout.title[i]);
        centred(c, fonts->tab, t.x, t.y, t.w, t.h, cap, current ? DESK_INK : DESK_MUTED);
    }
    // The status cluster: the master's word, the fan, the battery, then the
    // two targets. The word is what matters at a glance; the address is the
    // settings' business.
    char word[96];
    if (model->link == DESK_LINK_READY)
        snprintf(word, sizeof word, "%s \xc2\xb7 %s", model->master_name, link_words(model->link));
    else
        snprintf(word, sizeof word, "%s", link_words(model->link));
    right(c, fonts->small, 760, (DESK_BAR_H - text_h(fonts->small)) / 2, word,
          model->link == DESK_LINK_READY ? DESK_MUTED : DESK_WARN);
    icon_paint_wifi(c, 776, 12, model->wifi_bars, DESK_INK, DESK_LINE);
    icon_paint_battery(c, 816, 12, model->battery < 0 ? 0 : model->battery, model->charging, DESK_INK,
                       model->battery >= 0 && model->battery <= 20 ? DESK_WARN : DESK_INK, DESK_GLASS);
    char pct[16];
    if (model->battery >= 0)
        snprintf(pct, sizeof pct, "%d%%", model->battery);
    else
        snprintf(pct, sizeof pct, "--");
    text_at(c, fonts->small, 848, (DESK_BAR_H - text_h(fonts->small)) / 2, 60, pct, DESK_MUTED);
    struct desk_rect g = desk_view_gear(), l = desk_view_lock_target();
    if (model->setup_open)
        canvas_round_rect(c, g.x + 4, g.y + 4, g.w - 8, g.h - 8, 10, DESK_RAISED);
    icon_paint(c, ICON_GEAR, g.x + 12, g.y + 12, model->setup_open ? DESK_INK : DESK_MUTED);
    if (model->locked)
        canvas_round_rect(c, l.x + 4, l.y + 4, l.w - 8, l.h - 8, 10, DESK_INK);
    icon_paint(c, model->locked ? ICON_LOCK : ICON_LOCK_OPEN, l.x + 12, l.y + 12,
               model->locked ? DESK_GLASS : DESK_MUTED);
}

static void paint_headings(struct canvas *c, const struct desk_model *model,
                           const struct desk_fonts *fonts) {
    for (int i = 0; i < model->layout.headings; i++) {
        const struct desk_heading *h = &model->layout.heading[i];
        if (h->page != model->page || h->bank != model->bank)
            continue;
        // A section of hits held on the Mac says so in its title, once.
        int held_only = 1, any = 0;
        for (int j = 0; j < model->count; j++) {
            const struct desk_control *ctl = &model->control[j];
            if (ctl->page != model->page || ctl->section != h->section)
                continue;
            any = 1;
            if (ctl->enabled || strncmp(ctl->reason, "held", 4) != 0)
                held_only = 0;
        }
        char text[192];
        const char *elsewhere = NULL;
        if (h->parts > 1)
            for (int j = 0; j < model->layout.placements && !elsewhere; j++) {
                const struct desk_placement *p = &model->layout.placement[j];
                if (p->page != model->page || p->bank == model->bank || p->tile == TILE_MASTER ||
                    p->tile == TILE_PANIC)
                    continue;
                const struct desk_control *ctl = &model->control[p->control];
                if (ctl->section == h->section && ctl->state == DESK_ON)
                    elsewhere = ctl->label;
            }
        if (h->parts > 1 && elsewhere)
            snprintf(text, sizeof text, "%.60s  %d/%d  \xc2\xb7  %.47s", h->text, h->part, h->parts, elsewhere);
        else if (h->parts > 1)
            snprintf(text, sizeof text, "%.60s  %d/%d", h->text, h->part, h->parts);
        else
            snprintf(text, sizeof text, "%.60s%s", h->text, any && held_only ? "  \xc2\xb7  solo en el Mac" : "");
        desk_paint_section(c, fonts, h->x, h->y, h->w, text);
    }
}

// The pager, only when the page has more than one bank: small buttons at
// the bottom right, an amber dot on a bank where something runs.
static void paint_pager(struct canvas *c, const struct desk_model *model,
                        const struct desk_fonts *fonts) {
    int banks = model->layout.pages > 0 ? model->layout.banks[model->page] : 1;
    if (banks <= 1)
        return;
    for (int i = 0; i < banks; i++) {
        struct desk_rect b = desk_view_pager(i, banks);
        int current = i == model->bank;
        canvas_round_rect(c, b.x, b.y, b.w, b.h, 8, current ? DESK_RAISED : DESK_TILE);
        char digit[16];
        snprintf(digit, sizeof digit, "%d", i + 1);
        centred(c, fonts->tab, b.x, b.y, b.w, b.h, digit, current ? DESK_INK : DESK_MUTED);
        int running = 0;
        for (int j = 0; j < model->layout.placements && !running && i != model->bank; j++) {
            const struct desk_placement *p = &model->layout.placement[j];
            if (p->page == model->page && p->bank == i && p->tile != TILE_MASTER &&
                p->tile != TILE_PANIC && model->control[p->control].state == DESK_ON)
                running = 1;
        }
        if (running)
            canvas_round_rect(c, b.x + b.w - 12, b.y + 4, 8, 8, 4, DESK_AMBER);
    }
}

static void paint_link_banner(struct canvas *c, const struct desk_model *model,
                              const struct desk_fonts *fonts) {
    int x = DESK_CONTENT_X, w = DESK_CONTENT_W;
    char line[96];
    if (model->link == DESK_LINK_READY) {
        if (!model->mismatch)
            return;
        snprintf(line, sizeof line, "El Mac tiene otro show cargado: controles apagados");
    } else {
        snprintf(line, sizeof line, "%s%s - el Mac sigue al mando",
                 link_words(model->link), model->link == DESK_LINK_DOWN ? "" : " con el master");
    }
    canvas_round_rect(c, x, DESK_BAR_H + 4, w, 40, 10, DESK_WARN);
    centred(c, fonts->label, x, DESK_BAR_H + 4, w, 40, line, DESK_INK);
}

void desk_paint_overlays(struct canvas *canvas, const struct desk_model *model,
                         const struct desk_fonts *fonts) {
    paint_link_banner(canvas, model, fonts);
    if (model->locked) {
        int x = DESK_CONTENT_X, w = DESK_CONTENT_W;
        canvas_round_rect(canvas, x, DESK_H - 56, w, 40, 10, DESK_INK);
        centred(canvas, fonts->label, x, DESK_H - 56, w, 40,
                "Bloqueado - mantén el candado para desbloquear", DESK_GLASS);
    }
}

void desk_paint(struct canvas *canvas, const struct desk_model *model,
                const struct desk_fonts *fonts) {
    canvas_fill_rect(canvas, 0, 0, DESK_W, DESK_H, DESK_GLASS);
    paint_bar(canvas, model, fonts);
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
        case TILE_HOLD:    paint_hold(canvas, ctl, p, fonts, model->link); break;
        case TILE_COMPACT: paint_compact(canvas, ctl, p, fonts, model->link); break;
        default:           paint_cue(canvas, ctl, p, fonts, model->link); break;
        }
    }
    paint_pager(canvas, model, fonts);
    desk_paint_overlays(canvas, model, fonts);
}
