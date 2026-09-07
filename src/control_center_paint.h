#ifndef CONTROL_CENTER_PAINT_H
#define CONTROL_CENTER_PAINT_H
#include "canvas.h"
#include "control_center.h"
#include "control_center_layout.h"
#include "font.h"
struct cc_fonts { struct font *title, *value, *big, *label, *footer; };
/* Owns and clears the 560x400 straight-alpha canvas on each repaint. */
void cc_paint(struct canvas *c, const struct cc_model *m, const struct cc_fonts *f);
/* Update changed interior controls; previous must describe the current canvas. */
int cc_paint_update(struct canvas *c, const struct cc_model *m,
                    const struct cc_model *previous, const struct cc_fonts *fonts,
                    struct cc_rect *dirty);

#endif
