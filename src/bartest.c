#include <stdio.h>
#include <stdlib.h>
#include "canvas.h"
#include "status.h"
#include "statusbar.h"
int main(void) {
    int W = 1024, h = statusbar_height(W);
    struct canvas c = { calloc(W * h, 4), W, h };
    struct status st = { .have_batt = 1, .cap = 87, .ma = 120, .have_wifi = 1, .level = -40, .plugged = 1 };
    struct statusbar_style sty = { 0x00000000u, 0x00000000u, 0xFFFFFFFFu, 0x66FFFFFFu, 0xFFFFFFFFu,
        "br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf" };
    statusbar_paint(&c, &st, &sty);
    int n = 0; for (int i = 0; i < W * h; i++) if (c.px[i]) n++;
    printf("h=%d painted=%d\n", h, n);
    const char *out = getenv("OUT");
    FILE *f = fopen(out ? out : "/tmp/taq102-audit/bartest.ppm", "wb");
    if (!f) { perror("bar preview"); free(c.px); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", W, h);
    for (int i = 0; i < W * h; i++) { unsigned p = c.px[i]; unsigned a = p >> 24; unsigned char rgb[3] = { ((p>>16)&255)*a/255 + 0x12*(255-a)/255, ((p>>8)&255)*a/255 + 0x14*(255-a)/255, (p&255)*a/255 + 0x21*(255-a)/255 }; fwrite(rgb,1,3,f); }
    fclose(f); free(c.px); return 0;
}
