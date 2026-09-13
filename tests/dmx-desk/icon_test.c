// SOURCES: icon.c canvas.c canvas_blend.c
// Every icon blends something, nothing outside its 24 px box, and the
// battery's cell follows the level. Writes icons.ppm to look at.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "icon.h"

static int lit(const struct canvas *c, int x, int y, int w, int h) {
    int n = 0;
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (c->px[j * c->w + i] != 0xFF101012u)
                n++;
    return n;
}

int main(void) {
    struct canvas c = { .px = calloc(400 * 80, 4), .w = 400, .h = 80 };
    for (int i = 0; i < 400 * 80; i++)
        c.px[i] = 0xFF101012u;
    for (int id = 0; id < ICON_COUNT; id++) {
        icon_paint(&c, (enum icon_id)id, 8 + id * 32, 8, 0xFFF5F5F7u);
        assert(lit(&c, 8 + id * 32, 8, 24, 24) > 10);
        assert(lit(&c, 8 + id * 32 - 4, 4, 4, 32) == 0);
    }
    icon_paint_battery(&c, 8, 44, 100, 1, 0xFFF5F5F7u, 0xFFE08A00u, 0xFF101012u);
    icon_paint_battery(&c, 40, 44, 30, 0, 0xFFF5F5F7u, 0xFFC03020u, 0xFF101012u);
    icon_paint_wifi(&c, 72, 44, 2, 0xFFF5F5F7u, 0xFF404048u);
    assert(lit(&c, 8, 44, 24, 24) > lit(&c, 40, 44, 24, 24));
    const char *out = getenv("TEST_OUT");
    char path[512];
    snprintf(path, sizeof path, "%s/icons.ppm", out ? out : "/tmp");
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", c.w, c.h);
    for (int i = 0; i < c.w * c.h; i++) {
        uint32_t p = c.px[i];
        unsigned char rgb[3] = { (p >> 16) & 255, (p >> 8) & 255, p & 255 };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("icons ok\n");
    return 0;
}
