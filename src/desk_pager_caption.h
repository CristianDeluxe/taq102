#ifndef DESK_PAGER_CAPTION_H
#define DESK_PAGER_CAPTION_H

#include "font.h"
#include "showmap.h"

// A single line, cut at a UTF-8 boundary with an ellipsis when it does not fit.
void desk_pager_caption(struct font *font, const char *label, int width,
                        char out[MAP_CAPTION_MAX + 4]);

#endif
