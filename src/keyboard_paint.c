#include "keyboard_paint.h"

#include <string.h>

#include "desk_layout.h"
#include "keyboard_layout.h"

static void centred(struct canvas *c, struct font *f, int x, int y, int w, int h,
                    const char *text, uint32_t col) {
    if (!f) {
        int width = canvas_text_width(text, 3);
        canvas_text(c, x + (w - width) / 2, y + (h - 15) / 2, text, 3, col);
        return;
    }
    int width = font_width(f, text);
    if (width > w)
        width = w;
    font_draw_fit(f, c, x + (w - width) / 2, y + (h - font_height(f)) / 2 + font_baseline(f), w,
                  text, col);
}

void keyboard_paint(struct canvas *c, const struct keyboard *kb, const struct desk_fonts *fonts) {
    if (!kb->open)
        return;
    canvas_fill_rect(c, KB_SHEET_X, KB_SHEET_Y, KB_SHEET_W, KB_SHEET_H, DESK_GLASS);
    if (fonts->label)
        font_draw_fit(fonts->label, c, KB_FIELD_X, KB_TITLE_Y + font_baseline(fonts->label),
                      KB_FIELD_W, kb->title, DESK_MUTED);
    else
        canvas_text(c, KB_FIELD_X, KB_TITLE_Y, kb->title, 3, DESK_MUTED);

    // The field, with what the model says to show and a caret after it.
    canvas_round_rect(c, KB_FIELD_X, KB_FIELD_Y, KB_FIELD_W, KB_FIELD_H, KB_RADIUS, DESK_TILE);
    char shown[KB_TEXT_MAX + 1];
    keyboard_display(kb, shown, sizeof shown);
    int text_x = KB_FIELD_X + 16;
    if (fonts->tile) {
        int width = font_width(fonts->tile, shown);
        int max_w = KB_FIELD_W - 32;
        // A long passphrase scrolls: the tail is what the finger just typed.
        const char *tail = shown;
        while (width > max_w && *tail) {
            tail++;
            width = font_width(fonts->tile, tail);
        }
        font_draw_fit(fonts->tile, c, text_x, KB_FIELD_Y + (KB_FIELD_H - font_height(fonts->tile)) / 2 +
                      font_baseline(fonts->tile), max_w, tail, DESK_INK);
        canvas_fill_rect(c, text_x + width + 2, KB_FIELD_Y + 12, 2, KB_FIELD_H - 24, DESK_INK);
    } else {
        canvas_text(c, text_x, KB_FIELD_Y + 16, shown, 3, DESK_INK);
    }

    struct kb_key keys[KB_MAX_KEYS];
    int n = keyboard_keys(kb, keys, KB_MAX_KEYS);
    for (int i = 0; i < n; i++) {
        const struct kb_key *k = &keys[i];
        uint32_t fill = DESK_TILE;
        // Shift shows its state: amber when upper case is on, and it stays on
        // when locked; the layer key likewise names where it goes.
        // Ink, never amber: amber is the show's word and a key has none.
        // The primary key is raised with an ink ring, never a white slab.
        int primary = (k->kind == KB_KEY_DONE && k->enabled) || (k->kind == KB_KEY_SHIFT && kb->layer == KB_UPPER);
        if (primary)
            fill = DESK_RAISED;
        canvas_round_rect(c, k->x, k->y, k->w, k->h, KB_RADIUS, primary ? DESK_INK : fill);
        if (primary)
            canvas_round_rect(c, k->x + 2, k->y + 2, k->w - 4, k->h - 4, KB_RADIUS - 2, fill);
        if (i == kb->pressed) {
            canvas_round_rect(c, k->x, k->y, k->w, k->h, KB_RADIUS, DESK_INK);
            canvas_round_rect(c, k->x + 3, k->y + 3, k->w - 6, k->h - 6, KB_RADIUS - 3, fill);
        }
        uint32_t ink = k->enabled ? DESK_INK : DESK_MUTED;
        // The model's labels are the tests' names; the keys read in Spanish.
        const char *shown = k->label;
        static const char *const es[][2] = {
            { "done", "listo" }, { "cancel", "cancelar" }, { "show", "ver" }, { "space", "espacio" },
            { "shift", "may\xc3\xbas" }, { "CAPS", "MAY\xc3\x9aS" }, { "del", "borrar" },
        };
        for (size_t t = 0; t < sizeof es / sizeof es[0]; t++)
            if (strcmp(shown, es[t][0]) == 0)
                shown = es[t][1];
        struct font *f = strlen(k->label) == 1 ? fonts->tile : fonts->label;
        centred(c, f, k->x, k->y, k->w, k->h, shown, ink);
    }
}
