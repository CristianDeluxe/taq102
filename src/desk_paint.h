// Draws the model into a canvas: the bar with its tabs and status, the
// placements of the view on screen with their section rules, the pager, the
// master column, and the banners over everything when the master is gone or
// the surface is locked.
#ifndef DESK_PAINT_H
#define DESK_PAINT_H

#include "canvas.h"
#include "desk_model.h"
#include "desk_fonts.h"

// Paints the whole screen. Damage tracking belongs to the presenter, which
// knows which buffer is stale; the painter always draws everything.
void desk_paint(struct canvas *canvas, const struct desk_model *model,
                const struct desk_fonts *fonts);
// The link banner and the lock banner alone, for a page that paints its
// own content over the model's and must not bury them.
void desk_paint_overlays(struct canvas *canvas, const struct desk_model *model,
                         const struct desk_fonts *fonts);
// A section title with its rule, for pages that compose their own content.
void desk_paint_section(struct canvas *c, const struct desk_fonts *fonts, int x, int y, int w,
                        const char *title);

#endif
