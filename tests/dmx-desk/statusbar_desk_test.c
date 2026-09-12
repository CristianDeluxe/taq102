// SOURCES: canvas.c canvas_blend.c font.c statusbar.c
// The bar in the desk's palette: the charge and low colours are the caller's,
// and a battery the driver has not reported is drawn as unknown, not as 0%.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "statusbar.h"

int status_wifi_bars(const struct status *st) { return st->have_wifi ? 2 : 0; }

static int count(const struct canvas *c, uint32_t col) {
    int n = 0;
    for (int i = 0; i < c->w * c->h; i++)
        if (c->px[i] == col)
            n++;
    return n;
}

int main(void) {
    int w = 1024, h = statusbar_height(w);
    struct statusbar_style desk = { 0xFF141416u, 0xFF141416u, 0xFFF5F5F7u, 0xFF9A9AA0u,
        0xFFF5F5F7u, 0xFFE08A00u, 0xFFC03020u, NULL };
    struct canvas c = { .px = calloc((size_t)w * h, 4), .w = w, .h = h };

    // Charging paints the fill in the style's charge colour, never in green.
    struct status charging = { .have_batt = 1, .cap = 60, .plugged = 1, .have_wifi = 1 };
    statusbar_paint(&c, &charging, &desk);
    assert(count(&c, 0xFFE08A00u) > 20);
    assert(count(&c, 0xFF34C759u) == 0);

    // Low paints in the style's low colour.
    for (int i = 0; i < w * h; i++) c.px[i] = 0;
    struct status low = { .have_batt = 1, .cap = 15, .plugged = 0, .have_wifi = 1 };
    statusbar_paint(&c, &low, &desk);
    assert(count(&c, 0xFFC03020u) > 5);
    assert(count(&c, 0xFFFF3B30u) == 0);

    // No battery reading: no fill in either colour, and the bar still paints
    // its outline and text in ink.
    for (int i = 0; i < w * h; i++) c.px[i] = 0;
    struct status none = { .have_batt = 0, .cap = 0, .plugged = 0, .have_wifi = 1 };
    statusbar_paint(&c, &none, &desk);
    assert(count(&c, 0xFFE08A00u) == 0 && count(&c, 0xFFC03020u) == 0);
    assert(count(&c, 0xFFF5F5F7u) > 50);
    free(c.px);
    printf("statusbar_desk ok\n");
    return 0;
}
