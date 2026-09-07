#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>
#include "canvas_blend.h"
#include "font.h"

#define CACHE_LIMIT (2u * 1024 * 1024)
#define SLOT_COUNT 512

struct glyph {
    int cp, used, w, h, xoff, yoff, advance;
    unsigned char *mask;
};

struct font {
    unsigned char *file;
    stbtt_fontinfo info;
    float scale;
    int ascent, height, digit_advance;
    struct glyph slots[SLOT_COUNT];
    struct font *next;
};

static struct font *fonts;
static size_t cache_total;

static void clear_cache(struct font *f) {
    for (int i = 0; i < SLOT_COUNT; i++) {
        struct glyph *g = &f->slots[i];
        if (g->mask) {
            cache_total -= (size_t)g->w * g->h;
            stbtt_FreeBitmap(g->mask, NULL);
        }
    }
    memset(f->slots, 0, sizeof f->slots);
}

struct font *font_open(const char *path, int px) {
    if (!path || px <= 0 || px > 4096) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    struct font *f = calloc(1, sizeof *f);
    if (!f) { fclose(file); return NULL; }
    long len = -1;
    if (fseek(file, 0, SEEK_END) == 0) len = ftell(file);
    if (len < 12 || fseek(file, 0, SEEK_SET) != 0) goto fail;
    f->file = malloc((size_t)len);
    if (!f->file || fread(f->file, 1, (size_t)len, file) != (size_t)len) goto fail;
    // Installed faces are standalone sfnt files, not font collections.
    unsigned tables = (unsigned)f->file[4] * 256 + f->file[5];
    if (tables == 0 || 12u + 16u * tables > (size_t)len) goto fail;
    if (!stbtt_InitFont(&f->info, f->file, 0)) goto fail;
    int ascent, descent, gap, advance, bearing;
    stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &gap);
    if (ascent <= descent) goto fail;
    f->scale = stbtt_ScaleForPixelHeight(&f->info, (float)px);
    f->ascent = (int)ceilf(ascent * f->scale);
    f->height = f->ascent - (int)floorf(descent * f->scale);
    stbtt_GetCodepointHMetrics(&f->info, '0', &advance, &bearing);
    f->digit_advance = (int)lroundf(advance * f->scale);
    fclose(file);
    f->next = fonts;
    fonts = f;
    return f;
fail:
    fclose(file);
    free(f->file);
    free(f);
    return NULL;
}

void font_close(struct font *f) {
    if (!f) return;
    struct font **link = &fonts;
    while (*link && *link != f) link = &(*link)->next;
    if (*link) *link = f->next;
    clear_cache(f);
    free(f->file);
    free(f);
}

int font_height(const struct font *f) { return f ? f->height : 0; }
int font_baseline(const struct font *f) { return f ? f->ascent : 0; }
size_t font_cache_bytes(void) { return cache_total; }

static int next_cp(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    unsigned first = *p++;
    if (first < 128) { *text = (const char *)p; return (int)first; }
    int n = first >= 0xc2 && first <= 0xdf ? 1 :
            first >= 0xe0 && first <= 0xef ? 2 :
            first >= 0xf0 && first <= 0xf4 ? 3 : 0;
    unsigned cp = first & (n == 1 ? 31 : n == 2 ? 15 : 7);
    unsigned minimum = n == 1 ? 0x80 : n == 2 ? 0x800 : 0x10000;
    if (!n) { *text = (const char *)p; return 0xfffd; }
    for (int i = 0; i < n; i++) {
        // Inspect each byte only after its predecessor, including the terminator.
        if ((*p & 0xc0) != 0x80) { *text = (const char *)p; return 0xfffd; }
        cp = (cp << 6) | (*p++ & 63);
    }
    *text = (const char *)p;
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0xfffd;
    return cp <= 0xff || cp == 0x2026 ? (int)cp : 0xfffd;
}

static int face_cp(const struct font *f, int cp) {
    if (stbtt_FindGlyphIndex(&f->info, cp)) return cp;
    return stbtt_FindGlyphIndex(&f->info, 0xfffd) ? 0xfffd : '?';
}

static int advance_cp(const struct font *f, int cp) {
    if (cp >= '0' && cp <= '9') return f->digit_advance;
    int advance, bearing;
    stbtt_GetCodepointHMetrics(&f->info, face_cp(f, cp), &advance, &bearing);
    return (int)lroundf(advance * f->scale);
}

int font_width(const struct font *f, const char *utf8) {
    if (!f || !utf8) return 0;
    int width = 0;
    while (*utf8) {
        int advance = advance_cp(f, next_cp(&utf8));
        if (advance > INT_MAX - width) return INT_MAX;
        width += advance;
    }
    return width;
}

static struct glyph *lookup(struct font *f, int cp) {
    cp = face_cp(f, cp);
    unsigned slot = (unsigned)cp * 2654435761u % SLOT_COUNT;
    for (int i = 0; i < SLOT_COUNT; i++) {
        struct glyph *g = &f->slots[slot];
        if (!g->used) break;
        if (g->cp == cp) return g;
        slot = (slot + 1) % SLOT_COUNT;
        if (i == SLOT_COUNT - 1) clear_cache(f);
    }
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&f->info, cp, f->scale, f->scale, &x0, &y0, &x1, &y1);
    size_t bytes = (size_t)(x1 - x0) * (y1 - y0);
    if (bytes > CACHE_LIMIT) return NULL;
    if (cache_total + bytes > CACHE_LIMIT) {
        for (struct font *open = fonts; open; open = open->next) clear_cache(open);
        slot = (unsigned)cp * 2654435761u % SLOT_COUNT;
    }
    struct glyph *g = &f->slots[slot];
    g->mask = stbtt_GetCodepointBitmap(&f->info, f->scale, f->scale, cp,
                                     &g->w, &g->h, &g->xoff, &g->yoff);
    if (bytes && !g->mask) return NULL;
    int advance, bearing;
    stbtt_GetCodepointHMetrics(&f->info, cp, &advance, &bearing);
    g->advance = (int)lroundf(advance * f->scale);
    g->cp = cp;
    g->used = 1;
    if (g->mask) cache_total += (size_t)g->w * g->h;
    return g;
}

static void draw_text(struct font *f, struct canvas *c, int x, int y, const char *text,
                      uint32_t col, int64_t left, int64_t right) {
    int64_t pen = x;
    while (*text) {
        int cp = next_cp(&text);
        struct glyph *g = lookup(f, cp);
        if (g) {
            int shift = cp >= '0' && cp <= '9' ? (f->digit_advance - g->advance) / 2 : 0;
            for (int j = 0; j < g->h; j++) {
                int64_t py = (int64_t)y + g->yoff + j;
                if (py < 0 || py >= c->h) continue;
                for (int i = 0; i < g->w; i++) {
                    int64_t px = pen + shift + g->xoff + i;
                    if (px < 0 || px >= c->w || px < left || px >= right) continue;
                    unsigned a = (col >> 24) * g->mask[(size_t)j * g->w + i] / 255;
                    canvas_blend(c, (int)px, (int)py, (a << 24) | (col & 0xffffff));
                }
            }
        }
        pen += advance_cp(f, cp);
    }
}

void font_draw(struct font *f, struct canvas *c, int x, int y, const char *text, uint32_t col) {
    if (f && text) draw_text(f, c, x, y, text, col, 0, c->w);
}

void font_draw_fit(struct font *f, struct canvas *c, int x, int y, int max_w, const char *text, uint32_t col) {
    if (!f || !text || max_w <= 0) return;
    if (font_width(f, text) <= max_w) {
        draw_text(f, c, x, y, text, col, x, (int64_t)x + max_w);
        return;
    }
    int ellipsis = advance_cp(f, 0x2026);
    if (ellipsis > max_w) return;
    const char *end = text, *scan = text;
    int width = 0;
    while (*scan) {
        int advance = advance_cp(f, next_cp(&scan));
        if (advance > max_w - ellipsis - width) break;
        width += advance;
        end = scan;
    }
    size_t len = (size_t)(end - text);
    char *fit = malloc(len + 4);
    if (!fit) return;
    memcpy(fit, text, len);
    memcpy(fit + len, "\xe2\x80\xa6", 4);
    draw_text(f, c, x, y, fit, col, x, (int64_t)x + max_w);
    free(fit);
}
