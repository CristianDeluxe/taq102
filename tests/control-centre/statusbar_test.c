// SOURCES: canvas.c canvas_blend.c font.c statusbar.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "statusbar.h"
int status_wifi_bars(const struct status *st) { return st->have_wifi ? 3 : 0; }
static int lit(struct canvas *c) { int n = 0; for (int i = 0; i < c->w * c->h; i++) if (c->px[i]) n++; return n; }
int main(void) {
    int w = 1024, h = statusbar_height(w);
    struct status st = { .have_batt = 1, .cap = 87, .have_wifi = 1, .plugged = 1 };
    struct statusbar_style bitmap = { 0, 0, 0xFFFFFFFFu, 0x66FFFFFFu, 0xFFFFFFFFu, 0xFF34C759u, 0xFFFF3B30u, NULL };
    struct canvas a = { calloc(w * h, 4), w, h }; statusbar_paint(&a, &st, &bitmap);
    assert(lit(&a) > 100);
    struct statusbar_style inter = bitmap; inter.font_path = "br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf";

    struct canvas b = { calloc(w * h, 4), w, h }; statusbar_paint(&b, &st, &inter);
    int diff = 0; for (int i = 0; i < w * h; i++) if (a.px[i] != b.px[i]) diff++;
    assert(diff > 50);                                  // the digits changed face
    FILE *f = fopen("/tmp/taq102-audit/statusbar_inter.ppm", "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) { unsigned p = b.px[i], al = p >> 24; unsigned char rgb[3] = { ((p>>16)&255)*al/255, ((p>>8)&255)*al/255, (p&255)*al/255 }; fwrite(rgb, 1, 3, f); }
    fclose(f);
    struct statusbar_style missing = bitmap; missing.font_path = "/nonexistent.ttf";
    struct canvas fallback = { calloc(w * h, 4), w, h };
    statusbar_paint(&fallback, &st, &missing);
    for (int i = 0; i < w * h; i++) assert(a.px[i] == fallback.px[i]);
    // Subpixel coverage can average down to zero alpha on a transparent canvas.
    struct statusbar_style faint = { 0, 0, 0x01FFFFFFu, 0x01FFFFFFu, 0x01FFFFFFu, 0x01FFFFFFu, 0x01FFFFFFu,
        "br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf" };
    for (int i = 0; i < w * h; i++) fallback.px[i] = 0;
    statusbar_paint(&fallback, &st, &faint);
    free(fallback.px); free(a.px); free(b.px);
    printf("statusbar ok\n"); return 0;
}
