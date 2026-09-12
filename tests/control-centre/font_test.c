// SOURCES: canvas.c canvas_blend.c font.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canvas.h"
#include "font.h"
#define FONT "br2-external/package/taq102-fonts/fonts/Inter-SemiBold.ttf"
int main(void) {
    assert(font_open("/nonexistent.ttf", 20) == NULL);
    struct font *f = font_open(FONT, 28);
    assert(f && font_height(f) >= 28 && font_baseline(f) > 0 && font_baseline(f) < font_height(f));
    int w0 = font_width(f, "0"), w1 = font_width(f, "1"), w8 = font_width(f, "8");
    assert(w0 > 0 && w1 == w0 && w8 == w0);                          // equal digit cells
    assert(font_width(f, "100 %") > font_width(f, "10 %"));
    struct canvas c = { .px = calloc(200 * 60, 4), .w = 200, .h = 60 };
    for (int i = 0; i < 200 * 60; i++) c.px[i] = 0xFF141416u;
    font_draw(f, &c, 4, font_baseline(f) + 4, "84", 0xFFF5F5F7u);
    int lit = 0, edge = 0;
    for (int i = 0; i < 200 * 60; i++) { if (c.px[i] != 0xFF141416u) lit++; if ((c.px[i] >> 24) != 0xFF) edge++; }
    assert(lit > 50 && edge == 0);                                   // drawn, and the canvas stays opaque
    struct canvas t = { .px = calloc(200 * 60, 4), .w = 200, .h = 60 };
    font_draw(f, &t, 4, 40, "a", 0xFFFFFFFFu);
    int partial = 0;
    for (int i = 0; i < 200 * 60; i++) { unsigned a = t.px[i] >> 24; if (a && a < 255) partial++; }
    assert(partial > 0);                                             // anti-aliased edges over transparency
    font_draw_fit(f, &c, 0, 50, 40, "TestNet", 0xFFFFFFFFu);      // fits in 40 px by cutting with an ellipsis
    font_draw(f, &c, 0, 50, "caf\xc3\xa9 \xc2\xb7 \xe2\x80\xa6 \xe2\x82\xac", 0xFFFFFFFFu); // é, ·, …, € (fallback), no crash
    size_t before = font_cache_bytes();
    assert(before > 0 && before < 2u * 1024 * 1024);
    assert(font_width(f, "\xe2\x82\xac") == font_width(f, "\xef\xbf\xbd"));
    assert(font_width(f, "\xf0\x9f\x98\x80") == font_width(f, "\xef\xbf\xbd"));
    const char incomplete[] = { (char)0xe2, (char)0x80, 0 };
    assert(font_width(f, incomplete) > 0);
    font_draw(f, &t, -20, -10, incomplete, 0xFFFFFFFFu);
    memset(t.px, 0, 200 * 60 * 4);
    font_draw_fit(f, &t, 3, 40, 40, "caf\xc3\xa9 TestNet", 0xFFFFFFFFu);
    for (int y = 0; y < 60; y++) for (int x = 43; x < 200; x++)
        assert(t.px[y * 200 + x] == 0);
    memset(t.px, 0, 200 * 60 * 4);
    font_draw_fit(f, &t, 0, 40, 1, "TestNet", 0xFFFFFFFFu);
    for (int i = 0; i < 200 * 60; i++) assert(t.px[i] == 0);
    font_close(f);
    assert(font_cache_bytes() == 0);
    // Large faces force the shared budget to evict masks from earlier faces.
    struct font *large[3];
    int evicted = 0;
    for (int k = 0; k < 3; k++) {
        large[k] = font_open(FONT, 512);
        assert(large[k]);
        for (int cp = 33; cp < 127; cp++) {
            char str[2] = { (char)cp, 0 };
            size_t previous = font_cache_bytes();
            font_draw(large[k], &t, 0, 40, str, 0xFFFFFFFFu);
            if (font_cache_bytes() < previous) evicted = 1;
            assert(font_cache_bytes() <= 2u * 1024 * 1024);
        }
    }
    assert(evicted);
    font_draw(large[0], &t, 0, 40, "84", 0xFFFFFFFFu);
    size_t cached = font_cache_bytes();
    font_draw(large[0], &t, 0, 40, "84", 0xFFFFFFFFu);
    assert(font_cache_bytes() == cached);
    for (int k = 0; k < 3; k++) font_close(large[k]);
    assert(font_cache_bytes() == 0);
    font_close(NULL);
    free(c.px); free(t.px);
    printf("font ok\n"); return 0;
}
