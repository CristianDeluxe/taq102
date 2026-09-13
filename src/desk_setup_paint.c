#include "desk_setup_paint.h"

#include <stdio.h>
#include <string.h>

#include "canvas_blend.h"
#include "desk_layout.h"
#include "icon.h"
#include "desk_setup_layout.h"
#include "keyboard_paint.h"

static void text_at(struct canvas *c, struct font *f, int x, int y, int max_w, const char *s, uint32_t col) {
    if (f)
        font_draw_fit(f, c, x, y + font_baseline(f), max_w, s, col);
    else
        canvas_text(c, x, y, s, 3, col);
}

// The model keeps its words in English for the code and the tests; the
// sheet says them in the operator's language.
static const char *es(const char *word) {
    static const char *const table[][2] = {
        { "Scanning", "Buscando redes" }, { "Joining", "Uni\xc3\xa9ndose" }, { "Finding", "Buscando QLC+" },
        { "Scan refused", "Escaneo rechazado" }, { "Scan timed out", "Escaneo sin respuesta" },
        { "No network address", "Sin direcci\xc3\xb3n de red" }, { "Search failed", "B\xc3\xbasqueda fallida" },
        { "Search stopped", "B\xc3\xbasqueda parada" }, { "Applied, not saved", "Aplicado, sin guardar" },
        { "No QLC+ on this network", "Ning\xc3\xban QLC+ en esta red" }, { "Wrong key", "Clave incorrecta" },
        { "No association", "Sin asociaci\xc3\xb3n" }, { "No address", "Sin direcci\xc3\xb3n" },
        { "Associating", "Asociando" }, { "Getting an address", "Pidiendo direcci\xc3\xb3n" },
        { "Configuring network", "Configurando" }, { "Restoring network", "Restaurando la red" },
        { "Linked", "Enlazado" }, { "Connecting", "Conectando" }, { "No link", "Sin enlace" },
        { "Not linked", "Sin enlace" }, { "Reading the show", "Leyendo el show" },
        { "Network refused the tablet", "La red rechaz\xc3\xb3 la tablet" },
        { "Cannot renew the lease", "Sin renovar la direcci\xc3\xb3n" },
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(word, table[i][0]) == 0)
            return table[i][1];
    return word;
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

// Buttons in the desk's neutral palette: amber is the show's, never a
// button's. A primary action is ink on glass; a secondary one is ink on the
// sheet's own glass with a muted ring; a dead one is muted text alone.
enum button_style { BUTTON_PRIMARY, BUTTON_SECONDARY, BUTTON_DEAD };

static void button(struct canvas *c, struct font *f, int x, int y, int w, int h, const char *s,
                   enum button_style style) {
    if (style == BUTTON_PRIMARY) {
        // Raised with an ink ring, not a white slab: settings must not light
        // the operator's face in the dark.
        canvas_round_rect(c, x, y, w, h, 12, DESK_INK);
        canvas_round_rect(c, x + 2, y + 2, w - 4, h - 4, 10, DESK_RAISED);
        centred(c, f, x, y, w, h, s, DESK_INK);
    } else if (style == BUTTON_SECONDARY) {
        canvas_round_rect(c, x, y, w, h, 12, DESK_MUTED);
        canvas_round_rect(c, x + 2, y + 2, w - 4, h - 4, 10, DESK_GLASS);
        centred(c, f, x, y, w, h, s, DESK_INK);
    } else {
        centred(c, f, x, y, w, h, s, DESK_MUTED);
    }
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
        snprintf(line, sizeof line, "Sin control Wi-Fi");
    else if (s->ssid[0] && strcmp(s->wifi_state, "COMPLETED") == 0)
        snprintf(line, sizeof line, "%s  %s", s->ssid, s->address[0] ? s->address : "no address yet");
    else if (s->ssid[0])
        snprintf(line, sizeof line, "%s  %s", s->ssid, s->wifi_state);
    else
        snprintf(line, sizeof line, "Sin conectar");
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
    if (s->scan.count == 0 && s->wifi_available) {
        const char *empty = s->wifi_busy[0] ? "" : s->wifi_note[0] && strncmp(s->wifi_note, "Joined", 6) != 0
                          ? es(s->wifi_note) : "Sin redes a\xc3\xban: busca";
        text_at(c, fonts->label, x + 16, SETUP_ROWS_Y + 16, SETUP_CARD_W - 32, empty, DESK_MUTED);
    }
    // The note sits above the button: the last outcome; the button itself
    // says what it is doing while busy.
    char note_es[80];
    if (strncmp(s->wifi_note, "Joined ", 7) == 0)
        snprintf(note_es, sizeof note_es, "Unido a %s", s->wifi_note + 7);
    else
        snprintf(note_es, sizeof note_es, "%s", es(s->wifi_note));
    if (!s->wifi_busy[0])
        text_at(c, fonts->small, x + 16, SETUP_BUTTONS_Y - 28, SETUP_CARD_W - 32, note_es, DESK_MUTED);
    char busy[SETUP_WORD_MAX + 40];
    snprintf(busy, sizeof busy, "%s...", es(s->wifi_busy));
    button(c, fonts->label, x + 16, SETUP_BUTTONS_Y, SETUP_CARD_W - 32, SETUP_BUTTON_H,
           s->wifi_busy[0] ? busy : "Buscar redes",
           s->wifi_available && !s->wifi_busy[0] ? BUTTON_PRIMARY : BUTTON_DEAD);
}

static void master_card(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts) {
    int x = SETUP_MASTER_X;
    int pages = (s->found_count + SETUP_ROWS - 1) / SETUP_ROWS;
    card_frame(c, fonts, x, "Master QLC+", pages, s->found_page);
    char line[SETUP_HOST_MAX + 48];
    if (s->master_configured)
        snprintf(line, sizeof line, "%s:%d  %s", s->master, s->port, es(s->link_word));
    else
        snprintf(line, sizeof line, "Sin master");
    int cy = SETUP_CARD_Y + SETUP_TITLE_H;
    text_at(c, fonts->tile, x + 16, cy + (SETUP_CURRENT_H - text_h(fonts->tile)) / 2, SETUP_CARD_W - 32, line, DESK_INK);
    int shown = s->found_count - s->found_page * SETUP_ROWS;
    if (shown > SETUP_ROWS)
        shown = SETUP_ROWS;
    for (int r = 0; r < shown; r++) {
        const char *host = s->found[s->found_page * SETUP_ROWS + r];
        int port = s->found_port[s->found_page * SETUP_ROWS + r];
        if (!port)
            port = s->port;
        snprintf(line, sizeof line, "%s:%d", host, port);
        int ry = SETUP_ROWS_Y + r * SETUP_ROW_H;
        int current = s->master_configured && strcmp(host, s->master) == 0 && port == s->port;
        // The connected master carries a check, not a white row.
        canvas_round_rect(c, x + 8, ry + 4, SETUP_CARD_W - 16, SETUP_ROW_H - 8, 12, current ? DESK_RAISED : DESK_GLASS);
        text_at(c, fonts->tile, x + 24, ry + (SETUP_ROW_H - text_h(fonts->tile)) / 2, SETUP_CARD_W - 88, line,
                DESK_INK);
        if (current)
            icon_paint(c, ICON_CHECK, x + SETUP_CARD_W - 44, ry + (SETUP_ROW_H - 24) / 2, DESK_INK);
    }
    if (s->found_count == 0)
        text_at(c, fonts->label, x + 16, SETUP_ROWS_Y + 16, SETUP_CARD_W - 32,
                s->master_busy[0] ? "Barriendo la red" : s->master_note[0] ? es(s->master_note) :
                "Sin resultados de b\xc3\xbasqueda", DESK_MUTED);
    char note[160];
    const char *discovery = s->master_busy[0] ? "" : es(s->master_note);
    snprintf(note, sizeof note, "%s%s%s%s%s", es(s->save_note),
             s->save_note[0] && (discovery[0] || s->found_partial) ? "  " : "",
             discovery, discovery[0] && s->found_partial ? "  " : "",
             s->found_partial && !s->master_busy[0] ? "Solo los primeros 256" : "");
    text_at(c, fonts->small, x + 16, SETUP_BUTTONS_Y - 28, SETUP_CARD_W - 32, note, DESK_MUTED);
    int half = (SETUP_CARD_W - 48) / 2;
    char busy[SETUP_WORD_MAX + 40];
    snprintf(busy, sizeof busy, "%s...", es(s->master_busy));
    button(c, fonts->label, x + 16, SETUP_BUTTONS_Y, half, SETUP_BUTTON_H, s->master_busy[0] ? "Parar" : "Buscar QLC+",
           s->master_busy[0] ? BUTTON_SECONDARY : BUTTON_PRIMARY);
    if (s->master_busy[0])
        text_at(c, fonts->small, x + 16, SETUP_BUTTONS_Y - 28, SETUP_CARD_W - 32, busy, DESK_MUTED);
    button(c, fonts->label, x + 32 + half, SETUP_BUTTONS_Y, half, SETUP_BUTTON_H, "Escribir IP", BUTTON_SECONDARY);
}

static void footer(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts) {
    // The brightness: a slim track inside a generous touch region, the
    // label above it, no backing needed.
    canvas_round_rect(c, SETUP_FADER_X, SETUP_FADER_Y, SETUP_FADER_W, SETUP_FADER_H, 12, DESK_TILE);
    int track_y = SETUP_FADER_Y + SETUP_FADER_H - 36, track_h = 20;
    canvas_round_rect(c, SETUP_FADER_X + 16, track_y, SETUP_FADER_W - 32, track_h, 10, DESK_GLASS);
    int range = s->brightness_max - 8;
    int level = s->brightness - 8;
    int fill = range > 0 ? (SETUP_FADER_W - 1) * level / range : 0;
    // The fill is muted grey, so the label reads over both halves; the
    // number is the fact.
    int track_fill = (SETUP_FADER_W - 32) * level / (range > 0 ? range : 1);
    if (track_fill > 0)
        canvas_round_rect(c, SETUP_FADER_X + 16, track_y, track_fill, track_h, 10, DESK_MUTED);
    (void)fill;
    char line[48];
    int pct = range > 0 ? 100 * level / range : 100;
    snprintf(line, sizeof line, "Brillo %d%%%s", pct, s->brightness_unsaved ? "  \xc2\xb7 aplicado, sin guardar" : "");
    text_at(c, fonts->tile_s, SETUP_FADER_X + 16, SETUP_FADER_Y + 12, SETUP_FADER_W - 32, line, DESK_INK);
    // The toggle: a pill with a knob, the row its target.
    canvas_round_rect(c, SETUP_TOGGLE_X, SETUP_TOGGLE_Y, SETUP_TOGGLE_W, SETUP_TOGGLE_H, 12, DESK_TILE);
    int px = SETUP_TOGGLE_X + SETUP_TOGGLE_W - 24 - 52, py = SETUP_TOGGLE_Y + SETUP_TOGGLE_H - 40;
    canvas_round_rect(c, px, py, 52, 26, 13, s->power_aware ? DESK_INK : DESK_RAISED);
    canvas_round_rect(c, s->power_aware ? px + 52 - 24 : px + 2, py + 2, 22, 22, 11,
                      s->power_aware ? DESK_GLASS : DESK_MUTED);
    uint32_t ink = DESK_INK;
    text_at(c, fonts->small, SETUP_TOGGLE_X + 20, SETUP_TOGGLE_Y + 16, SETUP_TOGGLE_W - 40, "Atenuar con bater\xc3\xad" "a", ink);
    text_at(c, fonts->tile, SETUP_TOGGLE_X + 20, SETUP_TOGGLE_Y + SETUP_TOGGLE_H - 16 - text_h(fonts->tile),
            SETUP_TOGGLE_W - 40, s->power_aware ? "S\xc3\xad" : "No", ink);
}

static void confirm_sheet(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts) {
    canvas_round_rect(c, SETUP_CONFIRM_X, SETUP_CONFIRM_Y, SETUP_CONFIRM_W, SETUP_CONFIRM_H, 20, DESK_TILE);
    char line[WIFI_SSID_MAX + 24];
    snprintf(line, sizeof line, "\xc2\xbfUnirse a %s?", s->pending_ssid);
    text_at(c, fonts->tile, SETUP_CONFIRM_X + 24, SETUP_CONFIRM_Y + 28, SETUP_CONFIRM_W - 48, line, DESK_INK);
    text_at(c, fonts->label, SETUP_CONFIRM_X + 24, SETUP_CONFIRM_Y + 72, SETUP_CONFIRM_W - 48,
            "La tablet perder\xc3\xa1 la conexi\xc3\xb3n al cambiar de red; el show sigue en el Mac.", DESK_MUTED);
    text_at(c, fonts->small, SETUP_CONFIRM_X + 24, SETUP_CONFIRM_Y + 104, SETUP_CONFIRM_W - 48,
            s->confirm_is_open_network ? "Red abierta: sin clave."
            : s->confirm_psk[0] ? "La clave que has escrito se queda en la tablet."
            : "Se usa la clave guardada.", DESK_MUTED);
    int bx = SETUP_CONFIRM_X + 16, by = SETUP_CONFIRM_Y + SETUP_CONFIRM_H - 64;
    int buttons = s->confirm_known ? 3 : 2;
    int bw = (SETUP_CONFIRM_W - 32 - 16 * (buttons - 1)) / buttons;
    button(c, fonts->label, bx, by, bw, 48, "Cancelar", BUTTON_SECONDARY);
    if (buttons == 3)
        button(c, fonts->label, bx + bw + 16, by, bw, 48, "Otra clave", BUTTON_SECONDARY);
    button(c, fonts->label, bx + (buttons - 1) * (bw + 16), by, bw, 48, "Unirse", BUTTON_PRIMARY);
}

// A finger on a button: an ink outline where it is, as on the desk's tiles.
static void pressed_outline_ring(struct canvas *c, const struct desk_setup *s) {
    int x = 0, y = 0, w = 0, h = 0;
    int half = (SETUP_CARD_W - 48) / 2;
    switch (s->capture) {
    case T_SCAN: x = SETUP_WIFI_X + 16; y = SETUP_BUTTONS_Y; w = SETUP_CARD_W - 32; h = SETUP_BUTTON_H; break;
    case T_FIND: x = SETUP_MASTER_X + 16; y = SETUP_BUTTONS_Y; w = half; h = SETUP_BUTTON_H; break;
    case T_TYPE: x = SETUP_MASTER_X + 32 + half; y = SETUP_BUTTONS_Y; w = half; h = SETUP_BUTTON_H; break;
    case T_TOGGLE: x = SETUP_TOGGLE_X; y = SETUP_TOGGLE_Y; w = SETUP_TOGGLE_W; h = SETUP_TOGGLE_H; break;
    case T_CLOSE: x = SETUP_CLOSE_X; y = SETUP_CLOSE_Y; w = SETUP_CLOSE_W; h = SETUP_CLOSE_H; break;
    case T_WIFI_ROW:
        x = SETUP_WIFI_X + 8; y = SETUP_ROWS_Y + (s->capture_index - s->scan_page * SETUP_ROWS) * SETUP_ROW_H + 4;
        w = SETUP_CARD_W - 16; h = SETUP_ROW_H - 8; break;
    case T_MASTER_ROW:
        x = SETUP_MASTER_X + 8; y = SETUP_ROWS_Y + (s->capture_index - s->found_page * SETUP_ROWS) * SETUP_ROW_H + 4;
        w = SETUP_CARD_W - 16; h = SETUP_ROW_H - 8; break;
    default: return;
    }
    // A three-pixel ring of ink: four bars, so whatever fill is inside stays.
    canvas_fill_rect(c, x, y, w, 3, DESK_INK);
    canvas_fill_rect(c, x, y + h - 3, w, 3, DESK_INK);
    canvas_fill_rect(c, x, y, 3, h, DESK_INK);
    canvas_fill_rect(c, x + w - 3, y, 3, h, DESK_INK);
}

void desk_setup_paint(struct canvas *c, const struct desk_setup *s, const struct desk_fonts *fonts) {
    if (!s->open)
        return;
    if (s->kb.open) {
        keyboard_paint(c, &s->kb, fonts);
        return;
    }
    canvas_fill_rect(c, SETUP_SHEET_X, SETUP_SHEET_Y, SETUP_SHEET_W, SETUP_SHEET_H, DESK_GLASS);
    // The header: what this sheet is, and the way out where a hand expects it.
    text_at(c, fonts->tile, SETUP_WIFI_X, SETUP_HEADER_Y + (SETUP_HEADER_H - text_h(fonts->tile)) / 2,
            400, "Ajustes", DESK_INK);
    button(c, fonts->label, SETUP_CLOSE_X, SETUP_CLOSE_Y, SETUP_CLOSE_W, SETUP_CLOSE_H, "Cerrar", BUTTON_SECONDARY);
    wifi_card(c, s, fonts);
    master_card(c, s, fonts);
    footer(c, s, fonts);
    // The outline says a release here will do something: not off the
    // target, not on a dead one.
    int live = 1;
    if (s->capture == T_SCAN)
        live = s->wifi_available && !s->wifi_busy[0];
    else if (s->capture == T_FIND)
        live = 1;
    if (!s->confirm_open && s->capture != T_NONE && s->capture != T_FADER && !s->capture_outside && live)
        pressed_outline_ring(c, s);
    if (s->confirm_open) {
        // A uniform scrim over the cards, so the question stands alone.
        canvas_blend_rect(c, SETUP_SHEET_X, SETUP_SHEET_Y, SETUP_SHEET_W, SETUP_SHEET_H, 0xB0000000u | (DESK_GLASS & 0xFFFFFF));
        confirm_sheet(c, s, fonts);
    }
}
