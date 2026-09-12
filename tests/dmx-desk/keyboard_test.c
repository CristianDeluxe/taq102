// SOURCES: keyboard.c
// Every printable ASCII character is reachable; keys fire on release inside
// them; shift is one key unless locked; done waits for the minimum; the
// numeric layout types an address.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "keyboard.h"
#include "keyboard_layout.h"

static const struct kb_key *find_label(struct kb_key *keys, int n, const char *label) {
    for (int i = 0; i < n; i++)
        if (strcmp(keys[i].label, label) == 0)
            return &keys[i];
    return NULL;
}

// Taps the key with this label on the current layer.
static enum kb_result tap(struct keyboard *kb, const char *label) {
    struct kb_key keys[KB_MAX_KEYS];
    int n = keyboard_keys(kb, keys, KB_MAX_KEYS);
    const struct kb_key *k = find_label(keys, n, label);
    // The shift key reads CAPS while locked; a tap on "shift" means that cell.
    if (!k && strcmp(label, "shift") == 0)
        k = find_label(keys, n, "CAPS");
    assert(k);
    keyboard_touch_down(kb, k->x + 2, k->y + 2);
    return keyboard_touch_up(kb, k->x + 2, k->y + 2);
}

// Types one character by finding it on some layer.
static void type_char(struct keyboard *kb, char c) {
    char label[2] = { c, 0 };
    for (int attempt = 0; attempt < 6; attempt++) {
        struct kb_key keys[KB_MAX_KEYS];
        int n = keyboard_keys(kb, keys, KB_MAX_KEYS);
        // Past the maximum the key is found and refused: reachability is
        // the point of the loop, and the cap is checked afterwards.
        if (c == ' ') {
            enum kb_result r = tap(kb, "space");
            assert(r == KB_CHANGED || kb->len == kb->max_len);
            return;
        }
        if (find_label(keys, n, label)) {
            enum kb_result r = tap(kb, label);
            assert(r == KB_CHANGED || kb->len == kb->max_len);
            return;
        }
        // Not on this layer: shift for upper case, else cycle the layers.
        if (c >= 'A' && c <= 'Z' && kb->layer == KB_LOWER)
            tap(kb, "shift");
        else if (find_label(keys, n, "?123"))
            tap(kb, "?123");
        else if (find_label(keys, n, "#+="))
            tap(kb, "#+=");
        else
            tap(kb, "abc");
    }
    assert(!"character unreachable");
}

int main(void) {
    struct keyboard kb;
    keyboard_open(&kb, KB_TEXT, "Password for TestNet5", "", 1, 8, 63);
    assert(kb.open && kb.layout == KB_TEXT && kb.layer == KB_LOWER);
    struct kb_key keys[KB_MAX_KEYS];
    int n = keyboard_keys(&kb, keys, KB_MAX_KEYS);
    assert(n == 45);
    // Geometry: ten keys of 74 from x 16 to 828, rows 72 apart, all inside
    // the sheet and never over the master column.
    for (int i = 0; i < n; i++) {
        assert(keys[i].x >= KB_SHEET_X && keys[i].x + keys[i].w <= KB_SHEET_W);
        assert(keys[i].y >= KB_SHEET_Y && keys[i].y + keys[i].h <= KB_SHEET_Y + KB_SHEET_H);
        for (int j = i + 1; j < n; j++)
            assert(!(keys[i].x < keys[j].x + keys[j].w && keys[j].x < keys[i].x + keys[i].w &&
                     keys[i].y < keys[j].y + keys[j].h && keys[j].y < keys[i].y + keys[i].h));
    }
    assert(find_label(keys, n, "q")->x == 16 && find_label(keys, n, "p")->x == 16 + 9 * 82);
    assert(!keyboard_done_allowed(&kb));

    // Every printable ASCII character is reachable from some layer.
    for (int c = 0x20; c < 0x7f; c++)
        type_char(&kb, (char)c);
    assert(kb.len == 63 && kb.max_len == 63);    // 95 typed, capped at 63
    for (int c = 0x20; c < 0x20 + 63; c++)
        assert(kb.text[c - 0x20] == (char)c);

    // Masked until shown; done allowed now.
    char shown[128];
    keyboard_display(&kb, shown, sizeof shown);
    assert(shown[0] == '*' && strlen(shown) == 63);
    // Revealed while the show key is held, masked again on release.
    {
        struct kb_key keys[KB_MAX_KEYS];
        int n = keyboard_keys(&kb, keys, KB_MAX_KEYS), si = -1;
        for (int i = 0; i < n; i++)
            if (strcmp(keys[i].label, "show") == 0)
                si = i;
        assert(si >= 0);
        assert(keyboard_touch_down(&kb, keys[si].x + 2, keys[si].y + 2) == KB_CHANGED);
        keyboard_display(&kb, shown, sizeof shown);
        assert(strcmp(shown, kb.text) == 0);
        keyboard_touch_up(&kb, keys[si].x + 2, keys[si].y + 2);
        keyboard_display(&kb, shown, sizeof shown);
        assert(shown[0] == '*');
    }
    assert(keyboard_done_allowed(&kb));

    // Backspace, and a release outside the key fires nothing.
    while (kb.len > 0)
        assert(tap(&kb, "del") == KB_CHANGED);
    assert(tap(&kb, "del") == KB_NONE);
    type_char(&kb, 'a');
    n = keyboard_keys(&kb, keys, KB_MAX_KEYS);
    const struct kb_key *b = find_label(keys, n, "b");
    keyboard_touch_down(&kb, b->x + 2, b->y + 2);
    assert(keyboard_touch_up(&kb, b->x + b->w + 20, b->y + 2) == KB_NONE);
    assert(strcmp(kb.text, "a") == 0);

    // Shift: one key, then back; a second tap locks.
    tap(&kb, "shift");
    type_char(&kb, 'B');
    assert(kb.layer == KB_LOWER);
    tap(&kb, "shift");
    tap(&kb, "shift");
    assert(kb.shift_locked && kb.layer == KB_UPPER);
    type_char(&kb, 'C');
    type_char(&kb, 'D');
    assert(strcmp(kb.text, "aBCD") == 0 && kb.layer == KB_UPPER);
    tap(&kb, "shift");
    assert(!kb.shift_locked && kb.layer == KB_LOWER);

    // Done is dead below the minimum, live above; cancel leaves the text.
    assert(tap(&kb, "done") == KB_NONE);
    type_char(&kb, '1'); type_char(&kb, '2'); type_char(&kb, '3'); type_char(&kb, '4');
    assert(kb.len == 8 && tap(&kb, "done") == KB_DONE);
    assert(tap(&kb, "cancel") == KB_CANCEL && strcmp(kb.text, "aBCD1234") == 0);
    keyboard_close(&kb);
    assert(!kb.open && kb.text[0] == '\0');

    // The numeric layout types an address and a port.
    keyboard_open(&kb, KB_NUMERIC, "Master address", "192.168.1.", 0, 7, 21);
    n = keyboard_keys(&kb, keys, KB_MAX_KEYS);
    assert(n == 15);
    for (int i = 0; i < n; i++)
        assert(keys[i].x + keys[i].w <= KB_SHEET_W && keys[i].w >= 96);
    type_char(&kb, '6'); type_char(&kb, '5'); type_char(&kb, ':');
    type_char(&kb, '9'); type_char(&kb, '9'); type_char(&kb, '9'); type_char(&kb, '8');
    assert(strcmp(kb.text, "192.168.1.65:9998") == 0);
    assert(tap(&kb, "done") == KB_DONE);
    printf("keyboard ok\n");
    return 0;
}
