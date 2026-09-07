// An iOS-style status bar across the top of a canvas: a three-arc Wi-Fi fan
// lit by signal level, the battery percentage, and a battery with its charge
// as a fill, a bolt while current flows in. Every shape is a pixel test, so
// icons stay independent of the optional Inter face. The colours are the caller's: the
// rescue screen paints it amber on amber, glcube white over its own picture.
#ifndef STATUSBAR_H
#define STATUSBAR_H
#include <stdint.h>
#include "canvas.h"
#include "status.h"

struct statusbar_style {
    uint32_t bar;     // the strip behind the icons
    uint32_t hollow;  // the inside of the battery outline
    uint32_t ink;     // lit shapes and text
    uint32_t dim;     // unlit Wi-Fi arcs
    uint32_t pale;    // the charging bolt
    const char *font_path; // SemiBold TTF; NULL or unreadable keeps bitmap text
};

// The bar's height for a canvas w pixels wide; the caller sizes its canvas by it.
int statusbar_height(int w);
void statusbar_paint(struct canvas *c, const struct status *st, const struct statusbar_style *sty);

#endif
