#include "keyboard.h"

#include <stdio.h>
#include <string.h>

#include "keyboard_layout.h"

// Rows 1..3 of the text layout per layer, ten cells each; '\b' is the
// backspace cell and '\1' the shift cell (a caret is a character to type).
// The digit row is the same on every layer. Between them the layers cover
// every printable ASCII character: 26 letters twice, 10 digits, 32 symbols.
static const char *ROWS[4][3] = {
    { "qwertyuiop", "asdfghjkl\b", "\1zxcvbnm,." },
    { "QWERTYUIOP", "ASDFGHJKL\b", "\1ZXCVBNM,." },
    { "!@#$%^&*()", "-_=+[]{}\\\b", "\1;:'\"<>/,." },
    { "`~|?!@#$%^", "-_=+[]{}\\\b", "\1;:'\"<>/,." },
};
static const char DIGITS[] = "1234567890";

static void add(struct kb_key *out, int *n, int cap, int x, int y, int w, int h,
                enum kb_kind kind, char ch, const char *label, int enabled) {
    if (*n >= cap)
        return;
    struct kb_key *k = &out[(*n)++];
    k->x = x; k->y = y; k->w = w; k->h = h;
    k->kind = kind;
    k->ch = ch;
    snprintf(k->label, sizeof k->label, "%s", label);
    k->enabled = enabled;
}

static int text_keys(const struct keyboard *kb, struct kb_key *out, int cap) {
    int n = 0;
    for (int col = 0; col < 10; col++) {
        char label[2] = { DIGITS[col], 0 };
        add(out, &n, cap, KB_KEY_X(col), KB_ROW_Y(0), KB_KEY_W, KB_KEY_H, KEY_CHAR, DIGITS[col], label, 1);
    }
    for (int row = 0; row < 3; row++) {
        const char *cells = ROWS[kb->layer][row];
        for (int col = 0; col < 10; col++) {
            char c = cells[col];
            int x = KB_KEY_X(col), y = KB_ROW_Y(row + 1);
            if (c == '\b')
                add(out, &n, cap, x, y, KB_KEY_W, KB_KEY_H, KEY_BACKSPACE, 0, "del", 1);
            else if (c == '\1') {
                // On the symbol layers the shift cell goes back to letters.
                if (kb->layer >= KB_SYMBOLS_1)
                    add(out, &n, cap, x, y, KB_KEY_W, KB_KEY_H, KEY_LAYER, 0, "abc", 1);
                else
                    add(out, &n, cap, x, y, KB_KEY_W, KB_KEY_H, KEY_SHIFT, 0, "shift", 1);
            } else {
                char label[2] = { c, 0 };
                add(out, &n, cap, x, y, KB_KEY_W, KB_KEY_H, KEY_CHAR, c, label, 1);
            }
        }
    }
    // The bottom row: layer, space, cancel, show, done.
    int y = KB_ROW_Y(4);
    const char *layer_label = kb->layer == KB_SYMBOLS_1 ? "#+=" : kb->layer == KB_SYMBOLS_2 ? "abc" : "?123";
    add(out, &n, cap, KB_KEY_X(0), y, KB_KEY_W, KB_KEY_H, KEY_LAYER, 0, layer_label, 1);
    add(out, &n, cap, KB_KEY_X(1), y, 4 * KB_KEY_W + 3 * KB_GAP, KB_KEY_H, KEY_SPACE, ' ', "space", 1);
    add(out, &n, cap, KB_KEY_X(5), y, KB_KEY_W, KB_KEY_H, KEY_CANCEL, 0, "cancel", 1);
    add(out, &n, cap, KB_KEY_X(6), y, KB_KEY_W, KB_KEY_H, KEY_SHOW, 0, kb->show ? "hide" : "show",
        kb->masked);
    add(out, &n, cap, KB_KEY_X(7), y, 3 * KB_KEY_W + 2 * KB_GAP, KB_KEY_H, KEY_DONE, 0, "done",
        keyboard_done_allowed(kb));
    return n;
}

static int numeric_keys(const struct keyboard *kb, struct kb_key *out, int cap) {
    static const char *pad[4] = { "123", "456", "789", ".0:" };
    int n = 0;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            char label[2] = { pad[row][col], 0 };
            add(out, &n, cap, KB_NUM_X(col), KB_NUM_ROW_Y(row), KB_NUM_W, KB_NUM_H, KEY_CHAR,
                pad[row][col], label, 1);
        }
    }
    add(out, &n, cap, KB_NUM_X(3), KB_NUM_ROW_Y(0), KB_NUM_W, KB_NUM_H, KEY_BACKSPACE, 0, "del", 1);
    add(out, &n, cap, KB_NUM_X(0), KB_NUM_ROW_Y(4), KB_NUM_W, KB_NUM_H, KEY_CANCEL, 0, "cancel", 1);
    add(out, &n, cap, KB_NUM_X(1), KB_NUM_ROW_Y(4), 2 * KB_NUM_W + KB_GAP, KB_NUM_H, KEY_DONE, 0,
        "done", keyboard_done_allowed(kb));
    return n;
}

int keyboard_keys(const struct keyboard *kb, struct kb_key *out, int cap) {
    if (!kb->open)
        return 0;
    return kb->layout == KB_NUMERIC ? numeric_keys(kb, out, cap) : text_keys(kb, out, cap);
}

void keyboard_open(struct keyboard *kb, enum kb_layout layout, const char *title,
                   const char *initial, int masked, int min_len, int max_len) {
    memset(kb, 0, sizeof *kb);
    kb->open = 1;
    kb->layout = layout;
    kb->layer = KB_LOWER;
    snprintf(kb->title, sizeof kb->title, "%s", title ? title : "");
    snprintf(kb->text, sizeof kb->text, "%s", initial ? initial : "");
    kb->len = (int)strlen(kb->text);
    kb->masked = masked;
    kb->min_len = min_len;
    kb->max_len = max_len > 0 && max_len < KB_TEXT_MAX - 1 ? max_len : KB_TEXT_MAX - 1;
    kb->pressed = -1;
}

void keyboard_close(struct keyboard *kb) {
    // The text is wiped: a passphrase does not linger in memory once the
    // caller has taken it.
    memset(kb, 0, sizeof *kb);
    kb->pressed = -1;
}

static int key_at(const struct keyboard *kb, int x, int y) {
    struct kb_key keys[KB_MAX_KEYS];
    int n = keyboard_keys(kb, keys, KB_MAX_KEYS);
    for (int i = 0; i < n; i++) {
        const struct kb_key *k = &keys[i];
        if (x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h)
            return i;
    }
    return -1;
}

enum kb_result keyboard_touch_down(struct keyboard *kb, int x, int y) {
    if (!kb->open)
        return KB_NONE;
    kb->pressed = key_at(kb, x, y);
    return KB_NONE;
}

enum kb_result keyboard_touch_move(struct keyboard *kb, int x, int y) {
    (void)kb; (void)x; (void)y;   // a key fires on release inside it, so a move changes nothing
    return KB_NONE;
}

static enum kb_result type_char(struct keyboard *kb, char c) {
    if (kb->len >= kb->max_len)
        return KB_NONE;
    kb->text[kb->len++] = c;
    kb->text[kb->len] = '\0';
    // Shift is one key unless locked.
    if (kb->layer == KB_UPPER && !kb->shift_locked)
        kb->layer = KB_LOWER;
    return KB_CHANGED;
}

enum kb_result keyboard_touch_up(struct keyboard *kb, int x, int y) {
    if (!kb->open || kb->pressed < 0)
        return KB_NONE;
    int was = kb->pressed;
    kb->pressed = -1;
    if (key_at(kb, x, y) != was)
        return KB_NONE;
    struct kb_key keys[KB_MAX_KEYS];
    int n = keyboard_keys(kb, keys, KB_MAX_KEYS);
    if (was >= n)
        return KB_NONE;
    const struct kb_key *k = &keys[was];
    if (!k->enabled)
        return KB_NONE;
    switch (k->kind) {
    case KEY_CHAR:
    case KEY_SPACE:
        return type_char(kb, k->ch);
    case KEY_BACKSPACE:
        if (kb->len == 0)
            return KB_NONE;
        kb->text[--kb->len] = '\0';
        return KB_CHANGED;
    case KEY_SHIFT:
        if (kb->layer == KB_UPPER) {
            // A second tap locks; a third unlocks.
            if (kb->shift_locked) {
                kb->shift_locked = 0;
                kb->layer = KB_LOWER;
            } else {
                kb->shift_locked = 1;
            }
        } else {
            kb->layer = KB_UPPER;
            kb->shift_locked = 0;
        }
        return KB_CHANGED;
    case KEY_LAYER:
        kb->shift_locked = 0;
        kb->layer = kb->layer == KB_SYMBOLS_1 ? KB_SYMBOLS_2
                  : kb->layer == KB_SYMBOLS_2 ? KB_LOWER
                  : strcmp(k->label, "abc") == 0 ? KB_LOWER : KB_SYMBOLS_1;
        return KB_CHANGED;
    case KEY_SHOW:
        kb->show = !kb->show;
        return KB_CHANGED;
    case KEY_CANCEL:
        return KB_CANCEL;
    case KEY_DONE:
        return KB_DONE;
    }
    return KB_NONE;
}

void keyboard_touch_cancel(struct keyboard *kb) { kb->pressed = -1; }

void keyboard_display(const struct keyboard *kb, char *out, int cap) {
    if (cap <= 0)
        return;
    if (!kb->masked || kb->show) {
        snprintf(out, (size_t)cap, "%s", kb->text);
        return;
    }
    int n = kb->len < cap - 1 ? kb->len : cap - 1;
    for (int i = 0; i < n; i++)
        out[i] = '*';
    out[n] = '\0';
}

int keyboard_done_allowed(const struct keyboard *kb) {
    return kb->len >= kb->min_len;
}
