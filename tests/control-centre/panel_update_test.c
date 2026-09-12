// SOURCES: canvas.c canvas_blend.c font.c control_center.c control_center_paint.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "control_center_paint.h"
int main(void) {
    struct canvas full = { .px = calloc(560*400,4), .w = 560, .h = 400 }, partial = { .px = calloc(560*400,4), .w = 560, .h = 400 };
    uint32_t *uploaded = calloc(560*400,4);
    assert(full.px && partial.px && uploaded);
    struct cc_fonts fonts = {
        font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf",22),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf",28),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf",56),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-Regular.ttf",20),
        font_open("br2-external/package/taq102-fonts/fonts/Inter-Regular.ttf",16)};
    assert(fonts.title && fonts.value && fonts.big && fonts.label && fonts.footer);
    struct cc_rect dirty;
    for (int fallback = 0; fallback < 2; fallback++) {
        struct cc_model m; cc_init(&m,200,255,1,5);
        const struct cc_fonts *f = fallback ? NULL : &fonts;
        assert(cc_paint_update(&partial,&m,NULL,f,&dirty));
        memcpy(uploaded, partial.px, 560*400*4);
        for (int i = 0; i < 80; i++) {
            struct cc_model old = m;
            m.brightness = 8 + (i * 17) % 248; m.auto_on = i % 2;
            m.armed = i % 3; m.sleep_minutes = i % 2 ? 15 : 5;
            m.wifi = i % 5; m.level_dbm = -i; m.cap = i;
            snprintf(m.ssid,sizeof m.ssid,"Network %d",i);
            snprintf(m.addr,sizeof m.addr,"192.168.1.%d",i);
            snprintf(m.batt_word,sizeof m.batt_word,"%s",i%2 ? "Full" : "Charging");
            snprintf(m.build,sizeof m.build,"build-%d",i);
            snprintf(m.kernel,sizeof m.kernel,"4.4.%d",i);
            snprintf(m.note,sizeof m.note,"%s",i%2 ? "" : "Settings not saved");
            assert(cc_paint_update(&partial,&m,&old,f,&dirty));
            assert(dirty.x >= 416 && dirty.y >= 56 && dirty.w > 0 && dirty.h > 0);
            assert(dirty.x + dirty.w <= 976 && dirty.y + dirty.h <= 456);
            for (int row = 0; row < dirty.h; row++) {
                size_t at = (size_t)(dirty.y - 56 + row) * 560 + dirty.x - 416;
                memcpy(uploaded + at, partial.px + at, (size_t)dirty.w * 4);
            }
            cc_paint(&full,&m,f);
            assert(memcmp(full.px,uploaded,560*400*4) == 0);
            assert(memcmp(full.px,partial.px,560*400*4) == 0);
            if (m.wifi == WIFI_CONNECTED) {
                old = m; m.level_dbm--;
                assert(cc_paint_update(&partial,&m,&old,f,&dirty));
                assert(dirty.x == CC_WIFI_SIGNAL.x && dirty.w == CC_WIFI_SIGNAL.w);
                for (int row = 0; row < dirty.h; row++) {
                    size_t at = (size_t)(dirty.y - 56 + row) * 560 + dirty.x - 416;
                    memcpy(uploaded + at, partial.px + at, (size_t)dirty.w * 4);
                }
                cc_paint(&full,&m,f);
                assert(memcmp(full.px,uploaded,560*400*4) == 0);
            }
            old = m; m.open = !m.open; m.slide = .5; m.dirty = !m.dirty;
            assert(!cc_paint_update(&partial,&m,&old,f,&dirty));
        }
    }
    font_close(fonts.title); font_close(fonts.value); font_close(fonts.big);
    font_close(fonts.label); font_close(fonts.footer); free(full.px); free(partial.px); free(uploaded);
    puts("panel updates: 160 full-paint comparisons and geometry-only updates ok");
    return 0;
}
