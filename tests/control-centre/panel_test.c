// SOURCES: canvas.c canvas_blend.c font.c control_center.c control_center_paint.c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "control_center.h"
#include "control_center_layout.h"
#include "control_center_paint.h"

static const int64_t epoch = INT64_C(5000000000);
static struct touch_event event(enum touch_kind kind, struct cc_rect box, int slot) {
    return (struct touch_event){kind, slot, box.x + box.w / 2, box.y + box.h / 2, 0};
}
static int tap(struct cc_model *m, struct cc_rect box, int64_t time, struct cc_result *r) {
    struct touch_event d = event(TOUCH_DOWN, box, 0), u = event(TOUCH_UP, box, 0);
    assert(cc_handle(m, &d, time, r, 8) == 0);
    return cc_handle(m, &u, time + 100, r, 8);
}
static void model_tests(void) {
    struct cc_model m; struct cc_result r[8];
    cc_init(&m, 200, 255, 1, 5);
    assert(!cc_visible(&m));
    assert(tap(&m, CC_AUTO, epoch, r) == 0 && m.auto_on);
    cc_open(&m, epoch); assert(cc_visible(&m) && m.slide == 0);
    m.dirty = 0; cc_tick(&m, epoch + 90); assert(m.slide > 0 && m.slide < 1 && !m.dirty);
    float offset = cc_slide_offset(&m);
    cc_close(&m, epoch + 90); assert(cc_slide_offset(&m) == offset);
    cc_tick(&m, epoch + 135); offset = cc_slide_offset(&m);
    cc_open(&m, epoch + 135); assert(cc_slide_offset(&m) == offset);
    cc_tick(&m, epoch + 400); assert(m.slide == 1 && !m.dirty);
    assert(tap(&m, CC_AUTO, epoch + 1000, r) == 2);
    assert(r[0].action == CC_SET_AUTO && !r[0].arg && !m.auto_on && r[1].action == CC_SAVE_SETTINGS);
    struct touch_event d = event(TOUCH_DOWN, CC_AUTO, 0), u = event(TOUCH_UP, CC_AUTO, 0);
    cc_handle(&m, &d, epoch + 2000, r, 8);
    assert(cc_handle(&m, &u, epoch + 2401, r, 8) == 0);
    cc_handle(&m, &d, epoch + 2500, r, 8);
    u.slot = 1; assert(cc_handle(&m, &u, epoch + 2550, r, 8) == 0);
    u.slot = 0; u.x = CC_WIFI.x; assert(cc_handle(&m, &u, epoch + 2600, r, 8) == 0);
    cc_handle(&m, &d, epoch + 2700, r, 8);
    u = d; u.kind = TOUCH_CANCEL; assert(cc_handle(&m, &u, epoch + 2750, r, 8) == 0);
    u.kind = TOUCH_UP; assert(cc_handle(&m, &u, epoch + 2800, r, 8) == 0);
    cc_handle(&m, &d, epoch + 2900, r, 8);
    struct cc_model before = m;
    assert(cc_handle(&m, &u, epoch + 3000, r, 1) == -2);
    assert(memcmp(&before, &m, sizeof m) == 0);
    assert(cc_handle(&m, &u, epoch + 3000, r, 8) == 2 && m.auto_on);
    d = event(TOUCH_DOWN, CC_BRIGHT, 0); d.y = CC_BRIGHT.y + CC_BRIGHT.h - 1;
    int n = cc_handle(&m, &d, epoch + 4000, r, 8);
    assert(n == 2 && r[0].action == CC_SET_AUTO && r[1].action == CC_SET_BRIGHTNESS && m.brightness == 8);
    d.kind = TOUCH_MOVE; d.y = CC_BRIGHT.y;
    m.dirty = 0; assert(cc_handle(&m, &d, epoch + 4010, r, 8) == 0 && !m.dirty);
    assert(m.brightness == 8);
    d.kind = TOUCH_UP; n = cc_handle(&m, &d, epoch + 4020, r, 8);
    assert(n == 2 && r[0].action == CC_SET_BRIGHTNESS && r[0].arg == 255 && r[1].action == CC_SAVE_SETTINGS);
    d.kind = TOUCH_DOWN; cc_handle(&m, &d, epoch + 4100, r, 8);
    d.kind = TOUCH_MOVE; d.y = CC_BRIGHT.y + 164;
    assert(cc_handle(&m, &d, epoch + 4133, r, 8) == 1 && m.brightness > 100 && m.brightness < 160);
    d.kind = TOUCH_CANCEL; assert(cc_handle(&m, &d, epoch + 4140, r, 8) == 0 && !m.dragging_brightness);
    const int minutes[] = {1, 5, 15, 0};
    for (int i = 0; i < 4; i++) {
        assert(tap(&m, CC_TIMER_SEGMENT(i), epoch + 5000 + i * 200, r) == 2);
        assert(r[0].action == CC_SET_SLEEP_MINUTES && r[0].arg == minutes[i] && m.sleep_minutes == minutes[i] && r[1].action == CC_SAVE_SETTINGS);
    }
    assert(tap(&m, CC_WIFI, epoch + 6000, r) == 1 && r[0].action == CC_WIFI_TOGGLE);
    assert(r[0].arg == 1 && m.wifi == WIFI_STARTING);
    assert(tap(&m, CC_WIFI, epoch + 6200, r) == 1 && r[0].arg == 0 && m.wifi == WIFI_OFF);
    assert(tap(&m, CC_RESCUE, epoch + 7000, r) == 0 && m.armed == 1);
    assert(tap(&m, CC_RESCUE, epoch + 10000, r) == 0 && m.armed == 1);
    assert(tap(&m, CC_RESCUE, epoch + 10200, r) == 1 && r[0].action == CC_REBOOT_RESCUE && !m.armed);
    tap(&m, CC_LOADER, epoch + 11000, r); cc_tick(&m, epoch + 14100); assert(!m.armed);
    tap(&m, CC_LOADER, epoch + 15000, r);
    assert(tap(&m, CC_LOADER, epoch + 15200, r) == 1 && r[0].action == CC_REBOOT_LOADER);
    tap(&m, CC_RESCUE, epoch + 16000, r);
    assert(tap(&m, CC_OFFNOW, epoch + 16200, r) == 1 && r[0].action == CC_SLEEP_NOW && !m.armed);
    cc_open(&m, epoch + 17000); cc_tick(&m, epoch + 17200);
    tap(&m, CC_RESCUE, epoch + 18000, r); cc_close(&m, epoch + 18200); assert(!m.armed);
    cc_tick(&m, epoch + 18400); assert(!cc_visible(&m));
    cc_open(&m, epoch + 19000); cc_tick(&m, epoch + 19200);
    d = event(TOUCH_DOWN, CC_HANDLE, 0); cc_handle(&m, &d, epoch + 20000, r, 8);
    d.kind = TOUCH_MOVE; d.y -= 61; cc_handle(&m, &d, epoch + 20100, r, 8); assert(!m.open);
    cc_init(&m, -100, 4, 7, 999); assert(m.brightness_max >= 8 && m.brightness >= 8 && m.auto_on == 1 && m.sleep_minutes == 5);
}
static void snapshot(const char *name, struct canvas *c, struct cc_model *m, struct cc_fonts *fonts, FILE *manifest) {
    cc_paint(c, m, fonts);
    uint32_t *saved = malloc((size_t)c->w * c->h * 4); assert(saved);
    memcpy(saved, c->px, (size_t)c->w * c->h * 4);
    cc_paint(c, m, fonts); assert(memcmp(saved, c->px, (size_t)c->w * c->h * 4) == 0); free(saved);
    char path[256]; snprintf(path, sizeof path, "/tmp/taq102-audit/task7-%s.ppm", name);
    FILE *file = fopen(path, "wb"); assert(file); fprintf(file, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        unsigned p = c->px[i], a = p >> 24;
        unsigned char rgb[3] = {((p >> 16) & 255) * a / 255 + 0x2a * (255 - a) / 255,
            ((p >> 8) & 255) * a / 255 + 0x3a * (255 - a) / 255, (p & 255) * a / 255 + 0x55 * (255 - a) / 255};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    assert(fclose(file) == 0); fprintf(manifest, "/tmp/taq102-audit/task7-%s.png\n", name);
}
int main(void) {
    model_tests();
    struct cc_fonts f = {font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf", 22),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf", 28),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf", 56),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-Regular.ttf", 20),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-Regular.ttf", 16)};
    assert(f.title && f.value && f.big && f.label && f.footer);
    assert(font_width(f.label, "Brightness") <= CC_BRIGHT.w - 16);
    assert(font_width(f.label, "Tap again") <= CC_LOADER.w - 16);
    struct cc_model m; cc_init(&m, 214, 255, 1, 5);
    strcpy(m.ssid, "TestNet"); strcpy(m.addr, "192.168.1.57"); m.level_dbm = -37;
    m.cap = 100; m.plugged = 1; strcpy(m.batt_word, "Full"); strcpy(m.build, "20260907-162319-23127e9"); strcpy(m.kernel, "4.4.167");
    struct canvas c = {calloc(560 * 400, 4), 560, 400}; assert(c.px);
    FILE *manifest = fopen("/tmp/taq102-audit/task7-png-manifest.txt", "w"); assert(manifest);
    const char *states[] = {"wifi-off", "wifi-starting", "wifi-associating", "connected", "wifi-failed"};
    for (int i = 0; i < 5; i++) { m.wifi = (enum wifi_state)i; snapshot(states[i], &c, &m, &f, manifest); }
    m.wifi = WIFI_CONNECTED;
    int amber = 0; for (int i = 0; i < 560 * 400; i++) if ((c.px[i] & 0xffffff) == 0xe08a00) amber++; assert(amber > 2000);
    size_t warm = font_cache_bytes();
    for (int i = 8; i <= 255; i++) { m.brightness = i; cc_paint(&c, &m, &f); }
    assert(font_cache_bytes() == warm);
    m.brightness = 8; snapshot("brightness-min", &c, &m, &f, manifest);
    m.brightness = 255; snapshot("brightness-max", &c, &m, &f, manifest);
    m.brightness = 214; m.auto_on = 0; snapshot("auto-off", &c, &m, &f, manifest);
    const int minutes[] = {1, 5, 15, 0}; const char *timer[] = {"timer-1", "timer-5", "timer-15", "timer-never"};
    for (int i = 0; i < 4; i++) { m.sleep_minutes = minutes[i]; snapshot(timer[i], &c, &m, &f, manifest); }
    m.armed = 1; snapshot("rescue-armed", &c, &m, &f, manifest);
    m.armed = 2; snapshot("loader-armed", &c, &m, &f, manifest);
    strcpy(m.note, "cannot sustain"); snapshot("warning", &c, &m, &f, manifest);
    strcpy(m.ssid, "A very long network name 12345678"); strcpy(m.build, "20260907-162319-23127e9-extra-long-image-identifier");
    strcpy(m.note, "Settings could not be saved: storage unavailable"); snapshot("long-strings", &c, &m, &f, manifest);
    struct cc_fonts none = {0}; snapshot("bitmap-fallback", &c, &m, &none, manifest);
    fclose(manifest); free(c.px);
    font_close(f.title); font_close(f.value); font_close(f.big); font_close(f.label); font_close(f.footer);
    puts("panel ok: capture, actions, timing, animation, persistence, 17 deterministic visual states, warm glyph cache");
    return 0;
}
