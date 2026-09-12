#include "desk_setup_paint.h"

#include <stdio.h>
#include <string.h>

#include "desk_layout.h"
#include "desk_setup_layout.h"
#include "keyboard_paint.h"

static void text_at(struct canvas *c, struct font *f, int x, int y, int max_w, const char *s, uint32_t col) {
    if (f)
        font_draw_fit(f, c, x, y + font_baseline(f), max_w, s, col);
    else
        canvas_text(c, x, y, s, 3, col);
}

static int text_h(struct font *f) {
    return f ? font_height(f) : 15;
}

static void centred(struct canvas *c, struct font *f, int x, int y, int w, int h, const char *s, uint32_t col) {
    int width = f ? font_width(f, s) : canvas_text_width(s, 3);
    if (width > w)
        width = w;
    text_at(c, f, x + (w - width) / 2, y + (h - text_h(f)) / 2, w, s, col);
}

static void button(struct canvas *c, struct font *f, int x, int y, int w, int h, const char *s, int enabled) {
    canvas_round_rect(c, x, y, w, h, 12, enabled ? DESK_AMBER : DESK_TILE);
    centred(c, f, x, y, w, h, s, enabled ? DESK_GLASS : DESK_MUTED);
}

static void card_frame(struct canvas *c, const struct desk_fonts *fonts, int x, const char *title, int pages, int page) {
    canvas_round_rect(c, x, SETUP_CARD_Y, SETUP_CARD_W, SETUP_CARD_H, SETUP_CARD_RADIUS, DESK_TILE);
    text_at(c, fonts->label, x + 16, SETUP_CARD_Y + (SETUP_TITLE_H - text_h(fonts->label)) / 2 + 4,
            SETUP_CARD_W - 32 - 2 * SETUP_PAGE_W, title, DESK_MUTED);
    if (pages > 1) {
        int ax = x + SETUP_CARD_W - 2 * SETUP_PAGE_W - 8;
        centred(c, fonts->label, ax, SETUP_CARD_Y + 4, SETUP_PAGE_W, SETUP_TITLE_H, "<", page > 0 ? DESK_INK : DESK_MUTED);
        centred(c, fonts->label, ax + SETUP_PAGE_W, SETUP_CARD_Y + 4, SETUP_PAGE_W, SETUP_TITLE_H, ">",
                page + 1 < pages ? DESK_INK : DESK_MUTED);
    }
}

// Four bars from the level: the field's usual thresholds.
static void signal_bars(struct canvas *c, int x, int y, int dbm) {
    int lit = dbm >= -55 ? 4 : dbm >= -65 ? 3 : dbm >= -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int h = 6 + i * 5;
        canvas_fill_rect(c, x + i * 8, y + 22 - h, 6, h, i < lit ? DESK_INK : DESK_GLASS);
    }
}

static void wifi_card(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts) {
    int x = SETUP_WIFI_X;
    int pages = (s->scan.count + SETUP_ROWS - 1) / SETUP_ROWS;
    card_frame(c, fonts, x, "Wi-Fi", pages, s->scan_page);
    char line[128];
    int cy = SETUP_CARD_Y + SETUP_TITLE_H;
    if (!s->wifi_available)
        snprintf(line, sizeof line, "No Wi-Fi control");
    else if (s->ssid[0] && strcmp(s->wifi_state, "COMPLETED") == 0)
        snprintf(line, sizeof line, "%s  %s", s->ssid, s->address[0] ? s->address : "no address yet");
    else if (s->ssid[0])
        snprintf(line, sizeof line, "%s  %s", s->ssid, s->wifi_state);
    else
        snprintf(line, sizeof line, "Not connected");
    text_at(c, fonts->tile, x + 16, cy + (SETUP_CURRENT_H - text_h(fonts->tile)) / 2, SETUP_CARD_W - 32, line, DESK_INK);
    int shown = s->scan.count - s->scan_page * SETUP_ROWS;
    if (shown > SETUP_ROWS)
        shown = SETUP_ROWS;
    for (int r = 0; r < shown; r++) {
        const struct wifi_network *w = &s->scan.network[s->scan_page * SETUP_ROWS + r];
        int ry = SETUP_ROWS_Y + r * SETUP_ROW_H;
        int joinable = wifi_scan_joinable(w->security);
        canvas_round_rect(c, x + 8, ry + 4, SETUP_CARD_W - 16, SETUP_ROW_H - 8, 12, DESK_GLASS);
        signal_bars(c, x + 20, ry + 16, w->level_dbm);
        text_at(c, fonts->tile, x + 64, ry + (SETUP_ROW_H - text_h(fonts->tile)) / 2, SETUP_CARD_W - 150,
                w->ssid, joinable ? DESK_INK : DESK_MUTED);
        const char *tag = w->security == WIFI_OPEN ? "open" : w->security == WIFI_EAP ? "enterprise"
                        : w->security == WIFI_SAE_ONLY ? "WPA3 only" : s->known[s->scan_page * SETUP_ROWS + r] ? "known" : "";
        int tw = fonts->small ? font_width(fonts->small, tag) : canvas_text_width(tag, 3);
        text_at(c, fonts->small, x + SETUP_CARD_W - 20 - tw, ry + (SETUP_ROW_H - text_h(fonts->small)) / 2, 120, tag, DESK_MUTED);
    }
    if (s->scan.count == 0 && s->wifi_available)
        text_at(c, fonts->label, x + 16, SETUP_ROWS_Y + 16, SETUP_CARD_W - 32,
                s->wifi_busy[0] ? "Looking for networks" : "No networks yet", DESK_MUTED);
    // The note sits above the button: the last outcome or the busy word.
    const char *note = s->wifi_busy[0] ? s->wifi_busy : s->wifi_note;
    text_at(c, fonts->small, x + 16, SETUP_BUTTONS_Y - 28, SETUP_CARD_W - 32, note, DESK_MUTED);
    button(c, fonts->label, x + 16, SETUP_BUTTONS_Y, SETUP_CARD_W - 32, SETUP_BUTTON_H,
           s->wifi_busy[0] ? s->wifi_busy : "Scan", s->wifi_available && !s->wifi_busy[0]);
}

static void master_card(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts) {
    int x = SETUP_MASTER_X;
    int pages = (s->found_count + SETUP_ROWS - 1) / SETUP_ROWS;
    card_frame(c, fonts, x, "QLC+ master", pages, s->found_page);
    char line[SETUP_HOST_MAX + 48];
    if (s->master_configured)
        snprintf(line, sizeof line, "%s:%d  %s", s->master, s->port, s->link_word);
    else
        snprintf(line, sizeof line, "No master set");
    int cy = SETUP_CARD_Y + SETUP_TITLE_H;
    text_at(c, fonts->tile, x + 16, cy + (SETUP_CURRENT_H - text_h(fonts->tile)) / 2, SETUP_CARD_W - 32, line, DESK_INK);
    int shown = s->found_count - s->found_page * SETUP_ROWS;
    if (shown > SETUP_ROWS)
        shown = SETUP_ROWS;
    for (int r = 0; r < shown; r++) {
        const char *host = s->found[s->found_page * SETUP_ROWS + r];
        int ry = SETUP_ROWS_Y + r * SETUP_ROW_H;
        int current = s->master_configured && strcmp(host, s->master) == 0;
        canvas_round_rect(c, x + 8, ry + 4, SETUP_CARD_W - 16, SETUP_ROW_H - 8, 12, current ? DESK_AMBER : DESK_GLASS);
        text_at(c, fonts->tile, x + 24, ry + (SETUP_ROW_H - text_h(fonts->tile)) / 2, SETUP_CARD_W - 48, host,
                current ? DESK_GLASS : DESK_INK);
    }
    if (s->found_count == 0)
        text_at(c, fonts->label, x + 16, SETUP_ROWS_Y + 16, SETUP_CARD_W - 32,
                s->master_busy[0] ? "Sweeping the subnet" : "Nothing found yet", DESK_MUTED);
    if (s->found_partial)
        text_at(c, fonts->small, x + 16, SETUP_BUTTONS_Y - 28, SETUP_CARD_W - 32, "Large subnet: first 256 hosts", DESK_MUTED);
    else if (s->master_busy[0])
        text_at(c, fonts->small, x + 16, SETUP_BUTTONS_Y - 28, SETUP_CARD_W - 32, s->master_busy, DESK_MUTED);
    int half = (SETUP_CARD_W - 48) / 2;
    button(c, fonts->label, x + 16, SETUP_BUTTONS_Y, half, SETUP_BUTTON_H, s->master_busy[0] ? s->master_busy : "Find",
           !s->master_busy[0]);
    button(c, fonts->label, x + 32 + half, SETUP_BUTTONS_Y, half, SETUP_BUTTON_H, "Type address", 1);
}

static void footer(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts) {
    canvas_round_rect(c, SETUP_FADER_X, SETUP_FADER_Y, SETUP_FADER_W, SETUP_FADER_H, 20, DESK_TILE);
    int range = s->brightness_max - 8;
    int level = s->brightness - 8;
    int fill = range > 0 ? (SETUP_FADER_W - 1) * level / range : 0;
    if (fill > 0)
        canvas_round_rect(c, SETUP_FADER_X, SETUP_FADER_Y, fill + 1, SETUP_FADER_H, 20, DESK_AMBER);
    char line[48];
    int pct = range > 0 ? 100 * level / range : 100;
    snprintf(line, sizeof line, "Brightness %d%%", pct);
    text_at(c, fonts->tile, SETUP_FADER_X + 24, SETUP_FADER_Y + (SETUP_FADER_H - text_h(fonts->tile)) / 2,
            SETUP_FADER_W - 48, line, fill > 160 ? DESK_GLASS : DESK_INK);
    canvas_round_rect(c, SETUP_TOGGLE_X, SETUP_TOGGLE_Y, SETUP_TOGGLE_W, SETUP_TOGGLE_H, 20,
                      s->power_aware ? DESK_AMBER : DESK_TILE);
    uint32_t ink = s->power_aware ? DESK_GLASS : DESK_INK;
    text_at(c, fonts->small, SETUP_TOGGLE_X + 20, SETUP_TOGGLE_Y + 16, SETUP_TOGGLE_W - 40, "Dim on battery", ink);
    text_at(c, fonts->tile, SETUP_TOGGLE_X + 20, SETUP_TOGGLE_Y + SETUP_TOGGLE_H - 16 - text_h(fonts->tile),
            SETUP_TOGGLE_W - 40, s->power_aware ? "On" : "Off", ink);
}

static void confirm_sheet(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts) {
    canvas_round_rect(c, SETUP_CONFIRM_X, SETUP_CONFIRM_Y, SETUP_CONFIRM_W, SETUP_CONFIRM_H, 20, DESK_TILE);
    char line[WIFI_SSID_MAX + 24];
    snprintf(line, sizeof line, "Join %s?", s->pending_ssid);
    text_at(c, fonts->tile, SETUP_CONFIRM_X + 24, SETUP_CONFIRM_Y + 28, SETUP_CONFIRM_W - 48, line, DESK_INK);
    text_at(c, fonts->label, SETUP_CONFIRM_X + 24, SETUP_CONFIRM_Y + 72, SETUP_CONFIRM_W - 48,
            "Tablet controls disconnect; the Mac keeps running.", DESK_MUTED);
    text_at(c, fonts->small, SETUP_CONFIRM_X + 24, SETUP_CONFIRM_Y + 104, SETUP_CONFIRM_W - 48,
            s->confirm_is_open_network ? "An open network: no key."
            : s->confirm_psk[0] ? "The key you typed stays on the tablet."
            : "The key on file will be used.", DESK_MUTED);
    int bx = SETUP_CONFIRM_X + 16, by = SETUP_CONFIRM_Y + SETUP_CONFIRM_H - 64;
    int bw = (SETUP_CONFIRM_W - 48) / 2;
    button(c, fonts->label, bx, by, bw, 48, "Cancel", 0);
    button(c, fonts->label, bx + bw + 16, by, bw, 48, "Join", 1);
}

void desk_setup_paint(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts) {
    if (!s->open)
        return;
    if (s->kb.open) {
        keyboard_paint(c, &s->kb, fonts);
        return;
    }
    canvas_fill_rect(c, SETUP_SHEET_X, SETUP_SHEET_Y, SETUP_SHEET_W, SETUP_SHEET_H, DESK_GLASS);
    wifi_card(c, s, fonts);
    master_card(c, s, fonts);
    footer(c, s, fonts);
    if (s->confirm_open) {
        // Dim the cards under the sheet so the question stands alone.
        for (int y = SETUP_SHEET_Y; y < SETUP_SHEET_Y + SETUP_SHEET_H; y += 2)
            canvas_fill_rect(c, SETUP_SHEET_X, y, SETUP_SHEET_W, 1, DESK_GLASS);
        confirm_sheet(c, s, fonts);
    }
}
