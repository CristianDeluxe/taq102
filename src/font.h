#ifndef FONT_H
#define FONT_H

#include <stddef.h>
#include <stdint.h>
#include "canvas.h"

struct font;
// Fonts are trusted, installed TTF assets. Calls belong to the rendering thread.
struct font *font_open(const char *path, int px);
void font_close(struct font *f);
int font_height(const struct font *f);
int font_baseline(const struct font *f);
int font_width(const struct font *f, const char *utf8);
void font_draw(struct font *f, struct canvas *c, int x, int y_baseline, const char *utf8, uint32_t col);
void font_draw_fit(struct font *f, struct canvas *c, int x, int y_baseline, int max_w, const char *utf8, uint32_t col);
size_t font_cache_bytes(void);

#endif
