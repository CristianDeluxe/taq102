// SOURCES: canvas.c canvas_blend.c
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "canvas.h"
#include "canvas_blend.h"
int main(void) {
    assert(canvas_over(0x00000000u, 0x80FF0000u) == 0x80FF0000u);   // over transparent: source as is
    assert(canvas_over(0xFF0000FFu, 0xFFFF0000u) == 0xFFFF0000u);   // opaque source replaces
    uint32_t m = canvas_over(0xFF000000u, 0x80FFFFFFu);             // half white over black
    assert((m >> 24) == 0xFF && ((m >> 16) & 0xFF) >= 0x7F && ((m >> 16) & 0xFF) <= 0x81);
    assert(canvas_over(0xFF123456u, 0x00FFFFFFu) == 0xFF123456u);   // alpha 0 leaves dst
    struct canvas c = { .px = calloc(8 * 8, 4), .w = 8, .h = 8 };
    canvas_blend(&c, -1, 0, 0xFFFFFFFFu); canvas_blend(&c, 8, 8, 0xFFFFFFFFu);  // clipped, no crash
    canvas_blend_rect(&c, 2, 2, 4, 4, 0xFFE08A00u);
    assert(c.px[2 * 8 + 2] == 0xFFE08A00u && c.px[0] == 0);
    canvas_blend_round_rect(&c, 0, 0, 8, 8, 3, 0xFF0000FFu);
    assert(c.px[0] != 0xFF0000FFu);          // the corner is partially covered
    assert(c.px[3 * 8 + 3] == 0xFF0000FFu);  // the middle is fully covered
    printf("canvas_blend ok\n"); free(c.px); return 0;
}
