#include <string.h>
#include "canvas.h"

// 3x5 glyphs, three bits per row, bit 2 the left column: A..Z, 0..9, then
// '.', ':', '-', '/', '%'. Anything else advances without drawing.
static const unsigned char GLYPH[41][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,7,4,7},{7,4,7,4,4},
    {7,4,5,5,7},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,7},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{6,5,5,5,5},{7,5,5,5,7},{7,5,7,4,4},{7,5,5,7,1},{7,5,6,5,5},
    {7,4,7,1,7},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
    {0,0,0,0,2},{0,2,0,2,0},{0,0,7,0,0},{1,1,2,4,4},{5,1,2,4,5}
};

void canvas_put(struct canvas *c, int x, int y, uint32_t col) {
    if (x >= 0 && x < c->w && y >= 0 && y < c->h) c->px[y * c->w + x] = col;
}

void canvas_fill_rect(struct canvas *c, int x, int y, int w, int h, uint32_t col) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            canvas_put(c, x + i, y + j, col);
}

void canvas_round_rect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t col) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int cx = i < r ? r : (i >= w - r ? w - 1 - r : i);
            int cy = j < r ? r : (j >= h - r ? h - 1 - r : j);
            float dx = i - cx, dy = j - cy;
            if (dx * dx + dy * dy <= (float)r * r + 0.5f) canvas_put(c, x + i, y + j, col);
        }
}

static int glyph_index(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
    switch (ch) { case '.': return 36; case ':': return 37; case '-': return 38; case '/': return 39; case '%': return 40; }
    return -1;
}

void canvas_text(struct canvas *c, int x, int y, const char *str, int s, uint32_t col) {
    for (const char *p = str; *p; p++, x += 4 * s) {
        int g = glyph_index(*p);
        if (g < 0) continue;
        for (int r = 0; r < 5; r++)
            for (int k = 0; k < 3; k++)
                if (GLYPH[g][r] & (4 >> k))
                    canvas_fill_rect(c, x + k * s, y + r * s, s, s, col);
    }
}

int canvas_text_width(const char *str, int s) { return (int)strlen(str) * 4 * s - s; }

// 5x7 digits and '%', bit 4 the left column.
static const unsigned char DIGIT7[11][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x19,0x1A,0x02,0x04,0x08,0x0B,0x13},
};

void canvas_text7(struct canvas *c, int x, int y, const char *str, int s, uint32_t col) {
    for (const char *p = str; *p; p++, x += 6 * s) {
        int g = *p >= '0' && *p <= '9' ? *p - '0' : *p == '%' ? 10 : -1;
        if (g < 0) continue;
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++)
                if (DIGIT7[g][r] & (16 >> k))
                    canvas_fill_rect(c, x + k * s, y + r * s, s, s, col);
    }
}

int canvas_text7_width(const char *str, int s) { return (int)strlen(str) * 6 * s - s; }
