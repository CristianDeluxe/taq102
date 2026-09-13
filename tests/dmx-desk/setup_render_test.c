// SOURCES: desk_setup.c desk_setup_paint.c keyboard.c keyboard_paint.c wifi_scan.c desk_conf.c canvas.c canvas_blend.c font.c desk_fonts.c icon.c
// The settings surface painted: cards with a scan and found masters, the
// footer, the confirmation, and the keyboard for a key. Writes PPMs to look
// at; asserts only that painting touches the sheet and leaves the master
// column alone.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desk_layout.h"
#include "desk_setup.h"
#include "desk_setup_layout.h"
#include "desk_setup_paint.h"
#include "desk_fonts.h"
#include "font.h"

static void write_ppm(const struct canvas *c, const char *path) {
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        unsigned char rgb[3] = { (p >> 16) & 255, (p >> 8) & 255, p & 255 };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void save(const struct canvas *c, const char *name) {
    const char *out = getenv("TEST_OUT");
    char path[512];
    snprintf(path, sizeof path, "%s/%s", out ? out : "/tmp", name);
    write_ppm(c, path);
    printf("wrote %s\n", path);
}

int main(void) {
    struct desk_fonts fonts;
    assert(desk_fonts_open(&fonts, "br2-external/package/taq102-fonts/fonts") == 0);
    struct canvas canvas = { .px = calloc(DESK_W * DESK_H, 4), .w = DESK_W, .h = DESK_H };
    assert(canvas.px);
    const uint32_t sentinel = 0xFF123456u;
    for (int i = 0; i < DESK_W * DESK_H; i++)
        canvas.px[i] = sentinel;

    struct desk_setup s;
    desk_setup_init(&s);
    desk_setup_open(&s);
    s.wifi_available = 1;
    desk_setup_set_wifi(&s, "TestNet", "COMPLETED", "192.168.1.120");
    struct wifi_scan scan;
    memset(&scan, 0, sizeof scan);
    const char *names[] = { "TestNet", "TestNet5", "MOVISTAR_8A2F", "eduroam", "Cafe Central", "vodafone9F" };
    int levels[] = { -34, -36, -58, -66, -72, -81 };
    enum wifi_security sec[] = { WIFI_PSK, WIFI_PSK, WIFI_PSK, WIFI_EAP, WIFI_OPEN, WIFI_PSK };
    for (int i = 0; i < 6; i++) {
        strcpy(scan.network[i].ssid, names[i]);
        scan.network[i].level_dbm = levels[i];
        scan.network[i].security = sec[i];
    }
    scan.count = 6;
    int known[WIFI_SCAN_MAX] = { 1, 0, 0, 0, 0, 0 };
    desk_setup_set_scan(&s, &scan, known);
    snprintf(s.wifi_note, sizeof s.wifi_note, "Joined TestNet");
    char hosts[2][SETUP_HOST_MAX] = { "192.168.1.65", "192.168.1.56" };
    desk_setup_set_found(&s, hosts, 2, 0);
    desk_setup_set_master(&s, "192.168.1.65", 9999, 1);
    snprintf(s.link_word, sizeof s.link_word, "Linked 3 ms");
    s.brightness = 180;
    s.power_aware = 1;

    desk_setup_paint(&canvas, &s, &fonts);
    save(&canvas, "setup.ppm");
    // The master column is untouched; the sheet is painted.
    assert(canvas.px[100 * DESK_W + 900] == sentinel);
    assert(canvas.px[100 * DESK_W + 200] != sentinel);
    assert(canvas.px[20 * DESK_W + 200] == sentinel);   // the status bar is the caller's

    // The confirmation after a tap on TestNet5 and a typed key.
    desk_setup_touch_down(&s, SETUP_WIFI_X + 40, SETUP_ROWS_Y + SETUP_ROW_H + 10);
    desk_setup_touch_up(&s, SETUP_WIFI_X + 40, SETUP_ROWS_Y + SETUP_ROW_H + 10);
    assert(s.kb.open);
    desk_setup_paint(&canvas, &s, &fonts);
    save(&canvas, "setup-keyboard.ppm");
    snprintf(s.kb.text, sizeof s.kb.text, "correct horse");
    s.kb.len = (int)strlen(s.kb.text);
    desk_setup_paint(&canvas, &s, &fonts);
    save(&canvas, "setup-keyboard-typed.ppm");
    s.confirm_open = 1;
    snprintf(s.pending_ssid, sizeof s.pending_ssid, "TestNet5");
    snprintf(s.confirm_psk, sizeof s.confirm_psk, "correct horse");
    keyboard_close(&s.kb);
    desk_setup_paint(&canvas, &s, &fonts);
    save(&canvas, "setup-confirm.ppm");

    // Empty state: no Wi-Fi control, nothing found, no master.
    struct desk_setup e;
    desk_setup_init(&e);
    desk_setup_open(&e);
    desk_setup_paint(&canvas, &e, &fonts);
    save(&canvas, "setup-empty.ppm");
    printf("setup_render ok\n");
    return 0;
}
