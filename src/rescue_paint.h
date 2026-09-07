#ifndef RESCUE_PAINT_H
#define RESCUE_PAINT_H

#include "canvas.h"
#include "font.h"
#include "status.h"

void rescue_paint(struct canvas *c, const char *kernel, const char *build,
                  const struct status *st, struct font *title, struct font *body,
                  const char *bar_font_path);

#endif
