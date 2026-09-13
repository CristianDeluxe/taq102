// SOURCES: desk_setup_read_found.c desk_setup.c desk_brightness_track.c desk_brightness_level_at.c desk_setup_row_hit.c desk_setup_page_hit.c keyboard.c wifi_scan.c desk_conf.c desk_view.c
// The settings surface as a model: a tap on a network asks for its key, the
// key goes to a confirmation and out as one join action; a found master is
// one tap; the fader moves brightness; nothing fires while a job runs.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_setup.h"
#include "desk_setup_read_found.h"
#include "desk_setup_layout.h"
#include "keyboard.h"
#include "keyboard_layout.h"
#include "desk_brightness_track.h"
#include "desk_layout.h"

static struct setup_action tap(struct desk_setup *s, int x, int y) {
    desk_setup_touch_down(s, x, y);
    return desk_setup_touch_up(s, x, y);
}

static void type_on_keyboard(struct desk_setup *s, const char *text) {
    for (const char *p = text; *p; p++) {
        struct kb_key keys[KB_MAX_KEYS];
        int n = keyboard_keys(&s->kb, keys, KB_MAX_KEYS);
        char label[2] = { *p, 0 };
        const struct kb_key *k = NULL;
        for (int i = 0; i < n; i++)
            if (strcmp(keys[i].label, label) == 0)
                k = &keys[i];
        assert(k);
        tap(s, k->x + 2, k->y + 2);
    }
}

static struct setup_action press_key(struct desk_setup *s, const char *label) {
    struct kb_key keys[KB_MAX_KEYS];
    int n = keyboard_keys(&s->kb, keys, KB_MAX_KEYS);
    for (int i = 0; i < n; i++)
        if (strcmp(keys[i].label, label) == 0)
            return tap(s, keys[i].x + 2, keys[i].y + 2);
    assert(!"no such key");
    return tap(s, 0, 0);
}

int main(void) {
    struct desk_setup s;
    desk_setup_init(&s);
    desk_setup_open(&s);
    assert(s.open);
    struct desk_rect track=desk_brightness_track();
    int last=8;
    for(int x=track.x;x<track.x+track.w;x++) {
        struct setup_action level=desk_setup_touch_down(&s,x,track.y+track.h/2);
        assert(level.kind==SETUP_BRIGHTNESS && level.value>=last && level.value<=255);
        if(x<=track.x+8)assert(level.value==8);
        if(x>=track.x+track.w-1-8)assert(level.value==255);
        last=level.value;
        desk_setup_touch_up(&s,x,track.y+track.h/2);
    }
    assert(last==255);
    const int outside[][2]={{-1,-1},{-1,100},{100,-1},{DESK_W,100},{100,DESK_H},{-10000,10000}};
    for(unsigned i=0;i<sizeof outside/sizeof outside[0];i++) {
        assert(tap(&s,outside[i][0],outside[i][1]).kind==SETUP_NONE);
        assert(s.capture==T_NONE && s.open);
        desk_setup_touch_down(&s,4,60); // Valid outside-card dismissal press.
        assert(desk_setup_touch_up(&s,outside[i][0],outside[i][1]).kind==SETUP_NONE);
        desk_setup_touch_down(&s,track.x+track.w/2,track.y);
        int level=s.brightness;
        assert(desk_setup_touch_move(&s,outside[i][0],outside[i][1]).kind==SETUP_NONE);
        assert(s.brightness==level && s.capture==T_NONE);
    }
    // Without a supplicant the scan button is dead to touch as it is to the eye.
    assert(tap(&s, SETUP_WIFI_X + 20, SETUP_BUTTONS_Y + 10).kind == SETUP_NONE);
    s.wifi_available = 1;

    // Scan: the button asks once; while busy it is dead.
    int bx = SETUP_WIFI_X + 20, by = SETUP_BUTTONS_Y + 10;
    assert(tap(&s, bx, by).kind == SETUP_SCAN);
    snprintf(s.wifi_busy, sizeof s.wifi_busy, "Scanning");
    assert(tap(&s, bx, by).kind == SETUP_NONE);
    s.wifi_busy[0] = '\0';

    // Results: a WPA network asks for its key on the keyboard.
    struct wifi_scan scan;
    memset(&scan, 0, sizeof scan);
    strcpy(scan.network[0].ssid, "TestNet5"); scan.network[0].level_dbm = -36; scan.network[0].security = WIFI_PSK;
    strcpy(scan.network[1].ssid, "eduroam"); scan.network[1].level_dbm = -50; scan.network[1].security = WIFI_EAP;
    strcpy(scan.network[2].ssid, "Cafe"); scan.network[2].level_dbm = -60; scan.network[2].security = WIFI_OPEN;
    strcpy(scan.network[3].ssid, "TestNet"); scan.network[3].level_dbm = -34; scan.network[3].security = WIFI_PSK;
    scan.count = 4;
    int known[WIFI_SCAN_MAX] = { 0, 0, 0, 1 };
    desk_setup_set_scan(&s, &scan, known);
    int row0 = SETUP_ROWS_Y + 10;
    assert(tap(&s, SETUP_WIFI_X + 40, row0).kind == SETUP_NONE);
    assert(s.kb.open && s.kb_purpose == KB_FOR_PSK && s.kb.masked);
    assert(strstr(s.kb.title, "TestNet5"));
    // Done is dead until eight characters; then the confirmation shows.
    type_on_keyboard(&s, "abc");
    assert(press_key(&s, "done").kind == SETUP_NONE && s.kb.open);
    type_on_keyboard(&s, "defgh");
    assert(press_key(&s, "done").kind == SETUP_NONE);
    assert(!s.kb.open && s.confirm_open && strcmp(s.pending_ssid, "TestNet5") == 0);
    // Cancel closes the confirmation and forgets the key.
    int nx = SETUP_CONFIRM_X + 20, ny = SETUP_CONFIRM_Y + SETUP_CONFIRM_H - 60;
    assert(tap(&s, nx, ny).kind == SETUP_NONE && !s.confirm_open && s.confirm_psk[0] == '\0');
    // Again, and confirmed: one join action carrying ssid and key.
    tap(&s, SETUP_WIFI_X + 40, row0);
    type_on_keyboard(&s, "abcdefgh");
    press_key(&s, "done");
    int yx = SETUP_CONFIRM_X + SETUP_CONFIRM_W / 2 + 20;
    struct setup_action a = tap(&s, yx, ny);
    assert(a.kind == SETUP_JOIN && !a.known && strcmp(a.ssid, "TestNet5") == 0 && strcmp(a.psk, "abcdefgh") == 0);
    assert(strcmp(s.wifi_busy, "Joining") == 0 && s.confirm_psk[0] == '\0');
    s.wifi_busy[0] = '\0';
    // An enterprise network is not offered; an open one and a known one skip the keyboard.
    assert(tap(&s, SETUP_WIFI_X + 40, row0 + SETUP_ROW_H).kind == SETUP_NONE && !s.kb.open && !s.confirm_open);
    assert(tap(&s, SETUP_WIFI_X + 40, row0 + 2 * SETUP_ROW_H).kind == SETUP_NONE && s.confirm_open && s.confirm_is_open_network);
    a = tap(&s, yx, ny);
    assert(a.kind == SETUP_JOIN && strcmp(a.ssid, "Cafe") == 0 && a.psk[0] == '\0');
    s.wifi_busy[0] = '\0';
    // The fourth network is on the card's second page: page, then row one.
    int next_x = SETUP_WIFI_X + SETUP_CARD_W - SETUP_PAGE_W - 8 + 10, arrows_y = SETUP_CARD_Y + 10;
    assert(tap(&s, next_x, arrows_y).kind == SETUP_NONE && s.scan_page == 1);
    assert(tap(&s, SETUP_WIFI_X + 40, row0).kind == SETUP_NONE && s.confirm_open && !s.kb.open);
    // A known network's confirmation has three buttons: the last joins as is.
    int kx = SETUP_CONFIRM_X + SETUP_CONFIRM_W - 40;
    a = tap(&s, kx, ny);
    assert(a.kind == SETUP_JOIN && a.known && a.psk[0] == '\0' && strcmp(a.ssid, "TestNet") == 0);
    s.wifi_busy[0] = '\0';
    // The middle one asks for a new key, which then joins as a fresh block.
    tap(&s, SETUP_WIFI_X + 40, row0);
    assert(tap(&s, SETUP_CONFIRM_X + SETUP_CONFIRM_W / 2, ny).kind == SETUP_NONE && s.kb.open && s.kb_purpose == KB_FOR_PSK);
    type_on_keyboard(&s, "newkey12");
    press_key(&s, "done");
    assert(s.confirm_open && !s.confirm_known);
    a = tap(&s, yx, ny);
    assert(a.kind == SETUP_JOIN && !a.known && strcmp(a.psk, "newkey12") == 0 && strcmp(a.ssid, "TestNet") == 0);
    s.wifi_busy[0] = '\0';

    // The master card: find, a found row, a typed address.
    int fx = SETUP_MASTER_X + 20, tx = SETUP_MASTER_X + SETUP_CARD_W - 40;
    assert(tap(&s, fx, by).kind == SETUP_FIND);
    // While the sweep runs the same button asks to stop it.
    snprintf(s.master_busy, sizeof s.master_busy, "Finding");
    assert(tap(&s, fx, by).kind == SETUP_FIND_CANCEL);
    s.master_busy[0] = '\0';
    char hosts[2][SETUP_HOST_MAX] = { "192.168.1.65", "192.168.1.56" };
    desk_setup_set_found(&s, hosts, 2, 0);
    desk_setup_set_master(&s, "", 9998, 0);
    a = tap(&s, SETUP_MASTER_X + 40, row0 + SETUP_ROW_H);
    assert(a.kind == SETUP_SET_MASTER && strcmp(a.host, "192.168.1.56") == 0 && a.port == 9998);
    assert(tap(&s, tx, by).kind == SETUP_NONE && s.kb.open && s.kb.layout == KB_NUMERIC);
    press_key(&s, "del"); press_key(&s, "del");
    type_on_keyboard(&s, "2.7:9999");
    a = press_key(&s, "done");
    assert(a.kind == SETUP_SET_MASTER && strcmp(a.host, "192.168.2.7") == 0 && a.port == 9999);
    // A malformed address is refused and the keyboard stays, saying so.
    tap(&s, tx, by);
    for (int i = 0; i < 12; i++) press_key(&s, "del");
    type_on_keyboard(&s, "1.2.3.4:0");
    assert(press_key(&s, "done").kind == SETUP_NONE && s.kb.open && strstr(s.kb.title, "No es una direcci"));
    for (int i = 0; i < 12; i++) press_key(&s, "del");
    type_on_keyboard(&s, "1.2.3.999");
    assert(press_key(&s, "done").kind == SETUP_NONE && s.kb.open);
    for (int i = 0; i < 12; i++) press_key(&s, "del");
    type_on_keyboard(&s, "10.0.0.7");
    a = press_key(&s, "done");
    assert(a.kind == SETUP_SET_MASTER && strcmp(a.host, "10.0.0.7") == 0 && a.port == 9998 && !s.kb.open);
    // A list that changes under a pressed row drops the press.
    desk_setup_touch_down(&s, SETUP_MASTER_X + 40, row0);
    desk_setup_set_found(&s, hosts, 2, 0);
    assert(desk_setup_touch_up(&s, SETUP_MASTER_X + 40, row0).kind == SETUP_NONE);

    // The actual child protocol reaches row selection with the discovered port.
    FILE *found = tmpfile();
    assert(found);
    fputs("192.168.1.62:9998\n192.168.1.62:9999\npartial\n", found);
    rewind(found);
    desk_setup_read_found(&s, found, 0);
    assert(s.found_count == 2 && s.found_partial && !s.master_busy[0]);
    assert(s.found_port[0] == 9998 && s.found_port[1] == 9999);
    a = tap(&s, SETUP_MASTER_X + 40, row0 + SETUP_ROW_H);
    assert(a.kind == SETUP_SET_MASTER && !strcmp(a.host, "192.168.1.62") && a.port == 9999);
    fclose(found);

    found = tmpfile();
    assert(found);
    desk_setup_read_found(&s, found, 0);
    assert(!s.found_count && !strcmp(s.master_note, "No QLC+ answered scanned ports"));
    desk_setup_read_found(&s, found, 2);
    assert(!s.found_count && !strcmp(s.master_note, "Could not sweep"));
    desk_setup_read_found(&s, NULL, 0);
    assert(!strcmp(s.master_note, "Could not sweep"));
    snprintf(s.master_note, sizeof s.master_note, "Search stopped");
    desk_setup_read_found(&s, NULL, -1);
    assert(!strcmp(s.master_note, "Search stopped"));
    s.master_note[0] = '\0';
    fputs("partial\n", found);
    rewind(found);
    desk_setup_read_found(&s, found, 0);
    assert(s.found_partial && !strcmp(s.master_note, "No QLC+ in scanned range"));
    fclose(found);

    const char *invalid[] = { "192.168.1.62:0\n", "192.168.1.62:65536\n",
        "192.168.1.62:9998oops\n", "192.168.1.62:9998", "garbage\n",
        "partial\n192.168.1.62:9998\n", "192.168.1.62:9998\ngarbage\n" };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        found = tmpfile();
        assert(found);
        fputs(invalid[i], found);
        rewind(found);
        desk_setup_read_found(&s, found, 0);
        assert(!s.found_count && !strcmp(s.master_note, "Could not sweep"));
        fclose(found);
    }
    found = tmpfile();
    assert(found);
    fputs("192.168.1.62\n", found);
    rewind(found);
    desk_setup_read_found(&s, found, 0);
    assert(s.found_count == 1 && s.found_port[0] == s.port);
    rewind(found);
    desk_setup_read_found(&s, found, -1);
    assert(!s.found_count && !strcmp(s.master_note, "Could not sweep"));
    fclose(found);

    // The fader and the toggle.
    s.brightness_max = 255;
    a = desk_setup_touch_down(&s, SETUP_FADER_X + SETUP_FADER_W - 1, SETUP_FADER_Y + 10);
    assert(a.kind == SETUP_BRIGHTNESS && a.value == 255);
    a = desk_setup_touch_move(&s, SETUP_FADER_X, SETUP_FADER_Y + 10);
    assert(a.kind == SETUP_BRIGHTNESS && a.value == 8);
    assert(desk_setup_touch_up(&s, SETUP_FADER_X, SETUP_FADER_Y + 10).kind == SETUP_NONE);
    a = tap(&s, SETUP_TOGGLE_X + 10, SETUP_TOGGLE_Y + 10);
    assert(a.kind == SETUP_POWER_AWARE && a.value == 1 && s.power_aware);

    // A tap outside the cards closes the surface; so does the Close button.
    assert(tap(&s, 100, 484).kind == SETUP_CLOSE);
    assert(tap(&s, SETUP_CLOSE_X + 10, SETUP_CLOSE_Y + 10).kind == SETUP_CLOSE);
    desk_setup_close(&s);
    assert(!s.open && !s.kb.open);
    printf("desk_setup ok\n");
    return 0;
}
