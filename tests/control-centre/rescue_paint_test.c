// SOURCES: canvas.c canvas_blend.c font.c statusbar.c rescue_paint.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "canvas.h"
#include "font.h"
#include "rescue_paint.h"
#include "status.h"

int status_wifi_bars(const struct status *st) { return st->have_wifi ? 3 : 0; }

int main(void) {
    const char *semibold = "br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf";
    struct font *title = font_open(semibold, 56);
    struct font *body = font_open("br2-external/package/taq102-fonts/fonts/Inter-Regular.ttf", 20);
    assert(title && body);
    struct status st = { .have_batt = 1, .cap = 87, .mv = 4120, .ma = 120,
        .plugged = 1, .word = "Charging", .have_wifi = 1, .level = -40,
        .quality = 70, .addr = "192.168.1.57" };
    struct canvas inter = { calloc(1024 * 600, 4), 1024, 600 };
    struct canvas fallback = { calloc(1024 * 600, 4), 1024, 600 };
    assert(inter.px && fallback.px);
    rescue_paint(&inter, "4.4.167", "20260907-162319-23127e9", &st, title, body, semibold);
    rescue_paint(&fallback, "4.4.167", "20260907-162319-23127e9", &st, NULL, body, NULL);
    int different = 0;
    for (int i = 0; i < 1024 * 600; i++) {
        assert((inter.px[i] >> 24) == 255 && (fallback.px[i] >> 24) == 255);
        if (i >= 1024 * 48 && inter.px[i] != fallback.px[i]) different++;
    }
    assert(different > 1000);
    assert(inter.px[599 * 1024] == 0xFFE08A00u);
    FILE *out = fopen("/tmp/taq102-audit/rescue_inter.ppm", "wb");
    assert(out);
    fprintf(out, "P6\n1024 600\n255\n");
    for (int i = 0; i < 1024 * 600; i++) {
        uint32_t p = inter.px[i];
        unsigned char rgb[] = { p >> 16, p >> 8, p };
        assert(fwrite(rgb, 1, 3, out) == 3);
    }
    assert(fclose(out) == 0);
    rescue_paint(&inter, "4.4.167", "20260907-162319-23127e9", &st, title, NULL, NULL);
    for (int i = 0; i < 1024 * 600; i++) assert(inter.px[i] == fallback.px[i]);
    font_close(title); font_close(body);
    free(inter.px); free(fallback.px);
    puts("rescue_paint ok");
    return 0;
}
