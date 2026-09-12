#include "desk_setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desk_conf.h"
#include "desk_setup_layout.h"

enum setup_target {
    T_NONE, T_OUTSIDE, T_WIFI_ROW, T_WIFI_PREV, T_WIFI_NEXT, T_SCAN,
    T_MASTER_ROW, T_MASTER_PREV, T_MASTER_NEXT, T_FIND, T_TYPE,
    T_FADER, T_TOGGLE, T_CONFIRM_YES, T_CONFIRM_NO, T_CONFIRM_NEW_KEY, T_KEYBOARD,
};

static struct setup_action none(void) {
    struct setup_action a;
    memset(&a, 0, sizeof a);
    return a;
}

static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int rows_on_page(int count, int page) {
    int left = count - page * SETUP_ROWS;
    return left < 0 ? 0 : left > SETUP_ROWS ? SETUP_ROWS : left;
}

static enum setup_target hit(const struct desk_setup *s, int x, int y, int *index) {
    *index = -1;
    if (s->kb.open)
        return T_KEYBOARD;
    if (s->confirm_open) {
        int bx = SETUP_CONFIRM_X + 16, by = SETUP_CONFIRM_Y + SETUP_CONFIRM_H - 64;
        // A known network offers a third way: a new key for it.
        int buttons = s->confirm_known ? 3 : 2;
        int bw = (SETUP_CONFIRM_W - 32 - 16 * (buttons - 1)) / buttons;
        if (inside(x, y, bx, by, bw, 48))
            return T_CONFIRM_NO;
        if (buttons == 3 && inside(x, y, bx + bw + 16, by, bw, 48))
            return T_CONFIRM_NEW_KEY;
        if (inside(x, y, bx + (buttons - 1) * (bw + 16), by, bw, 48))
            return T_CONFIRM_YES;
        return T_NONE;
    }
    // The Wi-Fi card.
    if (inside(x, y, SETUP_WIFI_X, SETUP_CARD_Y, SETUP_CARD_W, SETUP_CARD_H)) {
        int arrows_x = SETUP_WIFI_X + SETUP_CARD_W - 2 * SETUP_PAGE_W - 8;
        if (inside(x, y, arrows_x, SETUP_CARD_Y, SETUP_PAGE_W, SETUP_TITLE_H))
            return T_WIFI_PREV;
        if (inside(x, y, arrows_x + SETUP_PAGE_W, SETUP_CARD_Y, SETUP_PAGE_W, SETUP_TITLE_H))
            return T_WIFI_NEXT;
        if (inside(x, y, SETUP_WIFI_X + 16, SETUP_BUTTONS_Y, SETUP_CARD_W - 32, SETUP_BUTTON_H))
            return T_SCAN;
        for (int r = 0; r < rows_on_page(s->scan.count, s->scan_page); r++) {
            if (inside(x, y, SETUP_WIFI_X, SETUP_ROWS_Y + r * SETUP_ROW_H, SETUP_CARD_W, SETUP_ROW_H)) {
                *index = s->scan_page * SETUP_ROWS + r;
                return T_WIFI_ROW;
            }
        }
        return T_NONE;
    }
    if (inside(x, y, SETUP_MASTER_X, SETUP_CARD_Y, SETUP_CARD_W, SETUP_CARD_H)) {
        int arrows_x = SETUP_MASTER_X + SETUP_CARD_W - 2 * SETUP_PAGE_W - 8;
        if (inside(x, y, arrows_x, SETUP_CARD_Y, SETUP_PAGE_W, SETUP_TITLE_H))
            return T_MASTER_PREV;
        if (inside(x, y, arrows_x + SETUP_PAGE_W, SETUP_CARD_Y, SETUP_PAGE_W, SETUP_TITLE_H))
            return T_MASTER_NEXT;
        int half = (SETUP_CARD_W - 48) / 2;
        if (inside(x, y, SETUP_MASTER_X + 16, SETUP_BUTTONS_Y, half, SETUP_BUTTON_H))
            return T_FIND;
        if (inside(x, y, SETUP_MASTER_X + 32 + half, SETUP_BUTTONS_Y, half, SETUP_BUTTON_H))
            return T_TYPE;
        for (int r = 0; r < rows_on_page(s->found_count, s->found_page); r++) {
            if (inside(x, y, SETUP_MASTER_X, SETUP_ROWS_Y + r * SETUP_ROW_H, SETUP_CARD_W, SETUP_ROW_H)) {
                *index = s->found_page * SETUP_ROWS + r;
                return T_MASTER_ROW;
            }
        }
        return T_NONE;
    }
    if (inside(x, y, SETUP_FADER_X, SETUP_FADER_Y, SETUP_FADER_W, SETUP_FADER_H))
        return T_FADER;
    if (inside(x, y, SETUP_TOGGLE_X, SETUP_TOGGLE_Y, SETUP_TOGGLE_W, SETUP_TOGGLE_H))
        return T_TOGGLE;
    return T_OUTSIDE;
}

void desk_setup_init(struct desk_setup *s) {
    memset(s, 0, sizeof *s);
    s->port = 9999;
    s->brightness_max = 255;
    s->brightness = 255;
    s->capture = T_NONE;
    s->capture_index = -1;
    snprintf(s->link_word, sizeof s->link_word, "No link");
}

void desk_setup_open(struct desk_setup *s) {
    s->open = 1;
    s->dirty = 1;
}

void desk_setup_close(struct desk_setup *s) {
    s->open = 0;
    keyboard_close(&s->kb);
    s->kb_purpose = KB_FOR_NOTHING;
    s->confirm_open = 0;
    memset(s->confirm_psk, 0, sizeof s->confirm_psk);
    s->capture = T_NONE;
    s->dragging_fader = 0;
    s->dirty = 1;
}

static int fader_level(const struct desk_setup *s, int x) {
    int span = SETUP_FADER_W - 1;
    int from_left = x - SETUP_FADER_X;
    if (from_left < 0)
        from_left = 0;
    if (from_left > span)
        from_left = span;
    int range = s->brightness_max - 8;
    return 8 + from_left * range / span;
}

struct setup_action desk_setup_touch_down(struct desk_setup *s, int x, int y) {
    if (!s->open || s->capture != T_NONE)
        return none();
    int index;
    enum setup_target t = hit(s, x, y, &index);
    s->capture = t;
    s->capture_index = index;
    if (t == T_KEYBOARD) {
        keyboard_touch_down(&s->kb, x, y);
        s->dirty = 1;
        return none();
    }
    if (t == T_FADER) {
        s->dragging_fader = 1;
        s->brightness = fader_level(s, x);
        s->dirty = 1;
        struct setup_action a = none();
        a.kind = SETUP_BRIGHTNESS;
        a.value = s->brightness;
        return a;
    }
    s->dirty = 1;
    return none();
}

struct setup_action desk_setup_touch_move(struct desk_setup *s, int x, int y) {
    (void)y;
    if (!s->open || !s->dragging_fader)
        return none();
    int level = fader_level(s, x);
    if (level == s->brightness)
        return none();
    s->brightness = level;
    s->dirty = 1;
    struct setup_action a = none();
    a.kind = SETUP_BRIGHTNESS;
    a.value = level;
    return a;
}

// The keyboard's result, turned into the surface's next step.
static struct setup_action keyboard_done(struct desk_setup *s) {
    struct setup_action a = none();
    if (s->kb_purpose == KB_FOR_PSK) {
        // The key goes to the confirmation and nowhere else.
        snprintf(s->confirm_psk, sizeof s->confirm_psk, "%.63s", s->kb.text);
        s->confirm_is_open_network = 0;
        s->confirm_known = 0;
        s->confirm_open = 1;
    } else if (s->kb_purpose == KB_FOR_HOST) {
        char host[SETUP_HOST_MAX];
        snprintf(host, sizeof host, "%s", s->kb.text);
        int port = s->port;
        char *colon = strrchr(host, ':');
        if (colon) {
            *colon = '\0';
            port = atoi(colon + 1);
        }
        // The port is the whole field after the colon, digits only.
        int port_ok = !colon;
        if (colon) {
            port_ok = colon[1] != '\0';
            for (const char *p = colon + 1; *p; p++)
                if (*p < '0' || *p > '9')
                    port_ok = 0;
        }
        if (desk_conf_valid_host(host) && port_ok && desk_conf_valid_port(port)) {
            a.kind = SETUP_SET_MASTER;
            snprintf(a.host, sizeof a.host, "%s", host);
            a.port = port;
        } else {
            // Not an address: the keyboard stays, and says why.
            snprintf(s->kb.title, sizeof s->kb.title, "Not an address: 192.168.1.65:9999 is one");
            s->dirty = 1;
            return a;
        }
    }
    keyboard_close(&s->kb);
    s->kb_purpose = KB_FOR_NOTHING;
    return a;
}

struct setup_action desk_setup_touch_up(struct desk_setup *s, int x, int y) {
    if (!s->open || s->capture == T_NONE)
        return none();
    enum setup_target was = s->capture;
    int was_index = s->capture_index;
    s->capture = T_NONE;
    s->capture_index = -1;
    s->dirty = 1;
    if (was == T_KEYBOARD) {
        enum kb_result r = keyboard_touch_up(&s->kb, x, y);
        if (r == KB_DONE)
            return keyboard_done(s);
        if (r == KB_CANCEL) {
            keyboard_close(&s->kb);
            s->kb_purpose = KB_FOR_NOTHING;
        }
        return none();
    }
    if (s->dragging_fader) {
        s->dragging_fader = 0;
        return none();
    }
    int index;
    if (hit(s, x, y, &index) != was || index != was_index)
        return none();
    struct setup_action a = none();
    switch (was) {
    case T_OUTSIDE:
        a.kind = SETUP_CLOSE;
        return a;
    case T_SCAN:
        if (s->wifi_busy[0] || !s->wifi_available)
            return none();
        a.kind = SETUP_SCAN;
        return a;
    case T_WIFI_ROW: {
        if (index < 0 || index >= s->scan.count || s->wifi_busy[0])
            return none();
        const struct wifi_network *w = &s->scan.network[index];
        if (!wifi_scan_joinable(w->security))
            return none();
        snprintf(s->pending_ssid, sizeof s->pending_ssid, "%s", w->ssid);
        if (w->security == WIFI_OPEN || s->known[index]) {
            // Known networks keep their key on file: no typing, just the ask.
            s->confirm_is_open_network = w->security == WIFI_OPEN;
            s->confirm_known = !s->confirm_is_open_network;
            s->confirm_psk[0] = '\0';
            s->confirm_open = 1;
            return none();
        }
        char title[80];
        snprintf(title, sizeof title, "Password for %s", w->ssid);
        keyboard_open(&s->kb, KB_TEXT, title, "", 1, 8, 63);
        s->kb_purpose = KB_FOR_PSK;
        return none();
    }
    case T_WIFI_PREV:
        if (s->scan_page > 0)
            s->scan_page--;
        return none();
    case T_WIFI_NEXT:
        if ((s->scan_page + 1) * SETUP_ROWS < s->scan.count)
            s->scan_page++;
        return none();
    case T_FIND:
        if (s->master_busy[0])
            return none();
        a.kind = SETUP_FIND;
        return a;
    case T_TYPE: {
        char initial[SETUP_HOST_MAX + 8];
        if (s->master_configured)
            snprintf(initial, sizeof initial, "%s:%d", s->master, s->port);
        else
            snprintf(initial, sizeof initial, "192.168.1.");
        keyboard_open(&s->kb, KB_NUMERIC, "Master address and port", initial, 0, 7, 24);
        s->kb_purpose = KB_FOR_HOST;
        return none();
    }
    case T_MASTER_ROW:
        if (index < 0 || index >= s->found_count)
            return none();
        a.kind = SETUP_SET_MASTER;
        snprintf(a.host, sizeof a.host, "%s", s->found[index]);
        a.port = s->port;
        return a;
    case T_MASTER_PREV:
        if (s->found_page > 0)
            s->found_page--;
        return none();
    case T_MASTER_NEXT:
        if ((s->found_page + 1) * SETUP_ROWS < s->found_count)
            s->found_page++;
        return none();
    case T_TOGGLE:
        s->power_aware = !s->power_aware;
        a.kind = SETUP_POWER_AWARE;
        a.value = s->power_aware;
        return a;
    case T_CONFIRM_YES:
        a.kind = SETUP_JOIN;
        snprintf(a.ssid, sizeof a.ssid, "%s", s->pending_ssid);
        snprintf(a.psk, sizeof a.psk, "%s", s->confirm_is_open_network ? "" : s->confirm_psk);
        a.known = s->confirm_known;
        s->confirm_open = 0;
        memset(s->confirm_psk, 0, sizeof s->confirm_psk);
        snprintf(s->wifi_busy, sizeof s->wifi_busy, "Joining");
        return a;
    case T_CONFIRM_NO:
        s->confirm_open = 0;
        memset(s->confirm_psk, 0, sizeof s->confirm_psk);
        return none();
    case T_CONFIRM_NEW_KEY: {
        // The block on file is replaced once the new key is typed and confirmed.
        s->confirm_open = 0;
        char title[80];
        snprintf(title, sizeof title, "New password for %s", s->pending_ssid);
        keyboard_open(&s->kb, KB_TEXT, title, "", 1, 8, 63);
        s->kb_purpose = KB_FOR_PSK;
        return none();
    }
    default:
        return none();
    }
}

void desk_setup_touch_cancel(struct desk_setup *s) {
    if (s->capture == T_KEYBOARD)
        keyboard_touch_cancel(&s->kb);
    s->capture = T_NONE;
    s->capture_index = -1;
    s->dragging_fader = 0;
    s->dirty = 1;
}

// A list that changes under a finger takes the finger's claim with it: a
// release must never resolve against a row that was not there when pressed.
static void drop_row_capture(struct desk_setup *s) {
    if (s->capture == T_WIFI_ROW || s->capture == T_MASTER_ROW) {
        s->capture = T_NONE;
        s->capture_index = -1;
    }
}

void desk_setup_set_scan(struct desk_setup *s, const struct wifi_scan *scan, const int *known) {
    drop_row_capture(s);
    s->scan = *scan;
    for (int i = 0; i < scan->count; i++)
        s->known[i] = known ? known[i] : 0;
    if (s->scan_page * SETUP_ROWS >= scan->count)
        s->scan_page = 0;
    s->dirty = 1;
}

void desk_setup_set_wifi(struct desk_setup *s, const char *ssid, const char *state, const char *address) {
    snprintf(s->ssid, sizeof s->ssid, "%s", ssid ? ssid : "");
    snprintf(s->wifi_state, sizeof s->wifi_state, "%s", state ? state : "");
    snprintf(s->address, sizeof s->address, "%s", address ? address : "");
    s->dirty = 1;
}

void desk_setup_set_found(struct desk_setup *s, const char (*hosts)[SETUP_HOST_MAX], int count, int partial) {
    drop_row_capture(s);
    s->found_count = count < SETUP_FOUND_MAX ? count : SETUP_FOUND_MAX;
    for (int i = 0; i < s->found_count; i++)
        snprintf(s->found[i], SETUP_HOST_MAX, "%s", hosts[i]);
    s->found_partial = partial;
    s->found_page = 0;
    s->dirty = 1;
}

void desk_setup_set_master(struct desk_setup *s, const char *host, int port, int configured) {
    snprintf(s->master, sizeof s->master, "%s", host ? host : "");
    s->port = port;
    s->master_configured = configured;
    s->dirty = 1;
}
