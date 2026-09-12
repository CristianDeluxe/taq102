// An on-screen keyboard as a model: a layout (text or numeric), a layer, a
// buffer and the keys with their rectangles, fed touches and answering with
// what a release did. A key fires on release inside it, like a cue, so
// sliding off is a way to change your mind. Pure; the painter reads it.
//
// The text layout is ASCII only on purpose: a WPA passphrase is 8 to 63
// printable ASCII characters, and that is what this keyboard exists to type.
#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KB_TEXT_MAX 80
#define KB_TITLE_MAX 64
#define KB_MAX_KEYS 64

enum kb_layout { KB_TEXT, KB_NUMERIC };
enum kb_layer { KB_LOWER, KB_UPPER, KB_SYMBOLS_1, KB_SYMBOLS_2 };
enum kb_result { KB_NONE, KB_CHANGED, KB_DONE, KB_CANCEL };
enum kb_kind { KB_KEY_CHAR, KB_KEY_BACKSPACE, KB_KEY_SHIFT, KB_KEY_LAYER, KB_KEY_SPACE, KB_KEY_CANCEL,
               KB_KEY_SHOW, KB_KEY_DONE };

struct kb_key {
    int x, y, w, h;
    enum kb_kind kind;
    char ch;            // KB_KEY_CHAR: the character it types
    char label[8];      // what the painter writes on it
    int enabled;        // Done below the minimum length is drawn but dead
};

struct keyboard {
    int open;
    enum kb_layout layout;
    enum kb_layer layer;
    int shift_locked;   // a second tap on shift locks upper case
    char title[KB_TITLE_MAX];
    char text[KB_TEXT_MAX];
    int len;
    int masked;         // a passphrase: dots unless `show`
    int show;
    int min_len, max_len;
    int pressed;        // index into keyboard_keys() while a finger is down, else -1
};

void keyboard_open(struct keyboard *kb, enum kb_layout layout, const char *title,
                   const char *initial, int masked, int min_len, int max_len);
void keyboard_close(struct keyboard *kb);

// The visible keys of the current layout and layer, with their rectangles.
int keyboard_keys(const struct keyboard *kb, struct kb_key *out, int cap);

enum kb_result keyboard_touch_down(struct keyboard *kb, int x, int y);
enum kb_result keyboard_touch_move(struct keyboard *kb, int x, int y);
enum kb_result keyboard_touch_up(struct keyboard *kb, int x, int y);
void keyboard_touch_cancel(struct keyboard *kb);

// What the field shows: the text, or dots for a masked one not shown.
void keyboard_display(const struct keyboard *kb, char *out, int cap);
int keyboard_done_allowed(const struct keyboard *kb);

#endif
