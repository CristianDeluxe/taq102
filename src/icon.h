// The desk's icons: anti-aliased alpha bitmaps generated from vectors by
// tools/icons.py, blended into a canvas in any colour. A battery is drawn
// as its shell plus its cell clipped to the level; the Wi-Fi fan as its dot
// plus as many arcs as the level lights.
#ifndef ICON_H
#define ICON_H

#include <stdint.h>

#include "canvas.h"
#include "icon_data.h"

// Blends one icon with its top-left at (x, y).
void icon_paint(struct canvas *c, enum icon_id id, int x, int y, uint32_t colour);
// The battery: shell in `ink`, the cell in `fill` to `level` (0..100), and a
// bolt over it while charging.
void icon_paint_battery(struct canvas *c, int x, int y, int level, int charging, uint32_t ink,
                        uint32_t fill, uint32_t bolt);
// The Wi-Fi fan with `bars` (0..3) of its arcs lit in `ink`, the rest in `dim`.
void icon_paint_wifi(struct canvas *c, int x, int y, int bars, uint32_t ink, uint32_t dim);

#endif
