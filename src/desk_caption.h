// A caption on at most two lines that fit a tile, measured against the face
// that will draw it rather than counted in characters: "Movimiento" is 104 px
// at Inter SemiBold 22 and "Lissajous (S)" 113, and only the font knows.
#ifndef DESK_CAPTION_H
#define DESK_CAPTION_H

#include "font.h"

#define DESK_CAPTION_LINE_MAX 48

struct desk_caption_lines {
    char line[2][DESK_CAPTION_LINE_MAX];
    int lines;      // 1 or 2
    int cut;        // 1 when text was dropped and the last line carries an ellipsis
};

// Wraps `text` at spaces into lines no wider than `width` in `font`. A NULL
// font measures the 3x5 glyph face at scale 3, which is what the painter
// falls back to. A single word wider than the line is cut with an ellipsis,
// and so is anything beyond the second line. Returns the lines used.
int desk_caption_fit(struct font *font, int width, const char *text,
                     struct desk_caption_lines *out);

#endif
