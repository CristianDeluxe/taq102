#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canvas_blend.h"
#include "control_center_layout.h"
#include "control_center_paint.h"

#define GLASS 0xC7141416u
#define TILE 0xEB2A2A2Eu
#define INK 0xFFF5F5F7u
#define MUTED 0xFF9A9AA0u
#define AMBER 0xFFE08A00u

static void rectangle(struct canvas *c, struct cc_rect r, int radius, uint32_t color) {
    canvas_blend_round_rect(c, r.x - CC_PANEL.x, r.y - CC_PANEL.y,
                            r.w, r.h, radius, color);
}

static void text(struct canvas *c, struct font *font, struct cc_rect box,
                 const char *value, uint32_t color, int centered, int baseline) {
    int width = font ? font_width(font, value) : canvas_text_width(value, 3);
    int x = box.x - CC_PANEL.x;
    if (centered && width < box.w) x += (box.w - width) / 2;
    if (font) {
        int y = baseline ? baseline - CC_PANEL.y :
            box.y - CC_PANEL.y + (box.h - font_height(font)) / 2 + font_baseline(font);
        font_draw_fit(font, c, x, y, box.w, value, color);
        return;
    }
    /* The legacy glyph writer overwrites pixels, so composite its mask instead. */
    struct canvas mask = {calloc((size_t)box.w * 15, sizeof(uint32_t)), box.w, 15};
    if (!mask.px) return;
    char fit[128]; size_t count = strlen(value);
    if (count >= sizeof fit) count = sizeof fit - 1;
    memcpy(fit, value, count); fit[count] = 0;
    while (count && canvas_text_width(fit, 3) > box.w) fit[--count] = 0;
    if (strlen(value) > count && count >= 3) memcpy(fit + count - 3, "...", 3);
    canvas_text(&mask, 0, 0, fit, 3, color);
    int y = baseline ? baseline - CC_PANEL.y - 15 : box.y - CC_PANEL.y + (box.h - 15) / 2;
    for (int j = 0; j < mask.h; j++)
        for (int i = 0; i < mask.w; i++)
            if (mask.px[j * mask.w + i]) canvas_blend(c, x + i, y + j, mask.px[j * mask.w + i]);
    free(mask.px);
}

static void warm(struct font *font) {
    /* Warming also repairs a cache cleared by another canvas between paints. */
    char ascii[96];
    for (int i = 0; i < 95; i++) ascii[i] = (char)(32 + i);
    ascii[95] = 0;
    struct canvas empty = {NULL, 0, 0};
    font_draw(font, &empty, 0, 0, ascii, INK);
    font_draw(font, &empty, 0, 0, "\xc2\xb7\xe2\x80\xa6", INK);
}

static void wifi(struct canvas *c, const struct cc_model *m, const struct cc_fonts *f) {
    rectangle(c, CC_WIFI, CC_TILE_RADIUS, TILE);
    text(c, f->title, CC_WIFI_TITLE, "Wi-Fi", INK, 0, 0);
    rectangle(c, CC_WIFI_DOT, CC_DOT_RADIUS, m->wifi == WIFI_CONNECTED ? AMBER : MUTED);
    const char *name = "Off", *detail = "Tap to turn on";
    switch (m->wifi) {
    case WIFI_STARTING: name = "Turning on…"; detail = m->ssid; break;
    case WIFI_ASSOCIATING: name = "Associating…"; detail = m->ssid; break;
    case WIFI_CONNECTED: name = m->ssid[0] ? m->ssid : "Connected"; detail = m->addr; break;
    case WIFI_FAILED: name = "Failed"; detail = "Tap to retry"; break;
    case WIFI_OFF: break;
    }
    text(c, f->value, CC_WIFI_NAME, name, INK, 0, 0);
    text(c, f->label, CC_WIFI_ADDRESS, detail, MUTED, 0, 0);
    if (m->wifi == WIFI_CONNECTED) {
        char signal[32]; snprintf(signal, sizeof signal, "%d dBm", m->level_dbm);
        text(c, f->label, CC_WIFI_SIGNAL, signal, MUTED, 0, 0);
    }
}

static void brightness(struct canvas *c, const struct cc_model *m, const struct cc_fonts *f) {
    rectangle(c, CC_BRIGHT, CC_TILE_RADIUS, TILE);
    int maximum = m->brightness_max < 8 ? 8 : m->brightness_max;
    int level = m->brightness < 8 ? 8 : m->brightness > maximum ? maximum : m->brightness;
    struct cc_rect fill = CC_BRIGHT_FILL;
    int height = maximum == 8 ? 0 : (int)((double)(level - 8) / (maximum - 8) * fill.h);
    fill.y += fill.h - height; fill.h = height;
    if (height) rectangle(c, fill, height < CC_FILL_RADIUS * 2 ? height / 2 : CC_FILL_RADIUS, AMBER);
    text(c, f->label, CC_BRIGHT_LABEL, "Brightness", INK, 1, 0);
    char number[16]; snprintf(number, sizeof number, "%d", (int)((double)level * 100 / maximum + .5));
    text(c, f->big, CC_BRIGHT_VALUE, number, INK, 1, 0);
    text(c, f->label, CC_BRIGHT_PERCENT, "%", INK, 1, 0);
}

static void action(struct canvas *c, struct cc_rect tile, const char *label,
                   int armed, const struct cc_fonts *f) {
    rectangle(c, tile, CC_TILE_RADIUS, armed ? AMBER : TILE);
    text(c, f->label, armed ? CC_ARMED_LABEL(tile) : CC_ACTION_LABEL(tile), label, INK, 1, 0);
    if (armed) text(c, f->label, CC_ARMED_HINT(tile), "Tap again", INK, 1, 0);
}

void cc_paint(struct canvas *c, const struct cc_model *m, const struct cc_fonts *fonts) {
    if (!c || !c->px || !m || c->w <= 0 || c->h <= 0) return;
    const struct cc_fonts none = {0};
    const struct cc_fonts *f = fonts ? fonts : &none;
    memset(c->px, 0, (size_t)c->w * c->h * sizeof *c->px);
    warm(f->title); warm(f->value); warm(f->big); warm(f->label); warm(f->footer);
    rectangle(c, CC_PANEL, CC_CARD_RADIUS, GLASS);
    rectangle(c, CC_HANDLE, CC_HANDLE_RADIUS, MUTED);
    wifi(c, m, f);
    brightness(c, m, f);
    rectangle(c, CC_AUTO, CC_TILE_RADIUS, m->auto_on ? AMBER : TILE);
    text(c, f->label, CC_AUTO_LABEL, "Auto", INK, 0, 0);
    rectangle(c, CC_AUTO_DOT, CC_DOT_RADIUS, m->auto_on ? INK : MUTED);
    rectangle(c, CC_OFFNOW, CC_TILE_RADIUS, TILE);
    text(c, f->label, CC_OFF_LABEL, "Screen off", INK, 1, 0);
    text(c, f->label, CC_OFF_NOW, "now", MUTED, 1, 0);
    action(c, CC_RESCUE, "Rescue", m->armed == 1, f);
    action(c, CC_LOADER, "Loader", m->armed == 2, f);
    rectangle(c, CC_TIMER, CC_TILE_RADIUS, TILE);
    text(c, f->label, CC_TIMER_TITLE, "Screen off after", MUTED, 0, 0);
    const int minutes[] = {1, 5, 15, 0};
    const char *labels[] = {"1", "5", "15", "never"};
    for (int i = 0; i < 4; i++) {
        struct cc_rect segment = CC_TIMER_SELECTION(i);
        if (m->sleep_minutes == minutes[i]) rectangle(c, segment, CC_SEGMENT_RADIUS, AMBER);
        text(c, f->label, segment, labels[i], INK, 1, 0);
    }
    if (m->note[0]) text(c, f->footer, CC_NOTE, m->note, AMBER, 0, 0);
    char footer[256];
    snprintf(footer, sizeof footer, "%d %% · %s · %s · %s", m->cap, m->batt_word,
             m->build, m->kernel);
    text(c, f->footer, CC_FOOTER, footer, MUTED, 0, CC_FOOTER_BASELINE);
}
