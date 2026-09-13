#include "desk_pager_caption.h"

#include <stdio.h>
#include <string.h>

#include "canvas.h"

void desk_pager_caption(struct font *font, const char *label, int width,
                        char out[MAP_CAPTION_MAX + 4]) {
    snprintf(out, MAP_CAPTION_MAX + 4, "%s", label);
    if ((font ? font_width(font, out) : canvas_text_width(out, 3)) <= width)
        return;
    // The fallback face has no Unicode ellipsis; three dots still say that
    // the name continues, and fit inside the same measured text budget.
    const char *ellipsis = font ? "\xe2\x80\xa6" : "...";
    size_t len = strlen(out);
    // Leave room for all three ellipsis bytes even if a caller supplied a
    // longer string than the map's bounded heading field.
    if (len > MAP_CAPTION_MAX - 1)
        len = MAP_CAPTION_MAX - 1;
    do {
        if (len > 0) {
            len--;
            while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80)
                len--;
        }
        strcpy(out + len, ellipsis);
        if ((font ? font_width(font, out) : canvas_text_width(out, 3)) <= width)
            return;
    } while (len > 0);
    out[0] = '\0';
}
