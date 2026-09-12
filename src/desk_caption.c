#include "desk_caption.h"

#include <stdio.h>
#include <string.h>

#include "canvas.h"

#define ELLIPSIS "\xe2\x80\xa6"

static int width_of(struct font *font, const char *text) {
    return font ? font_width(font, text) : canvas_text_width(text, 3);
}

// Cuts `text` to fit `width` with an ellipsis appended, at a UTF-8 boundary.
static void cut_to(struct font *font, int width, const char *text, char *out, size_t cap) {
    size_t len = strlen(text);
    while (len > 0) {
        while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80)
            len--;
        if (len + strlen(ELLIPSIS) >= cap) {
            len--;
            continue;
        }
        memcpy(out, text, len);
        strcpy(out + len, ELLIPSIS);
        if (width_of(font, out) <= width)
            return;
        len--;
    }
    strcpy(out, ELLIPSIS);
}

int desk_caption_fit(struct font *font, int width, const char *text,
                     struct desk_caption_lines *out) {
    memset(out, 0, sizeof *out);
    if (!text)
        text = "";
    char rest[256];
    snprintf(rest, sizeof rest, "%s", text);

    for (int line = 0; line < 2; line++) {
        // Skip leading spaces.
        char *start = rest;
        while (*start == ' ')
            start++;
        if (!*start) {
            break;
        }
        // The longest prefix ending at a space that fits.
        size_t best = 0;
        size_t len = strlen(start);
        for (size_t i = 1; i <= len; i++) {
            if (i < len && start[i] != ' ')
                continue;
            char probe[256];
            memcpy(probe, start, i);
            probe[i] = '\0';
            if (width_of(font, probe) <= width)
                best = i;
            else
                break;
        }
        int last = line == 1;
        if (best == 0) {
            // One word wider than the line: it is cut, and nothing follows.
            cut_to(font, width, start, out->line[line], sizeof out->line[line]);
            out->cut = 1;
            out->lines = line + 1;
            return out->lines;
        }
        if (last && best < len) {
            // More text than a second line holds: the second line is cut.
            cut_to(font, width, start, out->line[line], sizeof out->line[line]);
            out->cut = 1;
            out->lines = 2;
            return 2;
        }
        size_t copy = best < sizeof out->line[line] ? best : sizeof out->line[line] - 1;
        memcpy(out->line[line], start, copy);
        out->line[line][copy] = '\0';
        out->lines = line + 1;
        memmove(rest, start + best, len - best + 1);
    }
    // Anything left after two lines was already handled; nothing left means
    // the caption fit whole.
    return out->lines;
}
