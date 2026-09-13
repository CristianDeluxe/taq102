#ifndef DESK_PAGER_BANK_CAPTION_H
#define DESK_PAGER_BANK_CAPTION_H

#include "desk_model.h"
#include "font.h"

#define DESK_PAGER_BANK_CAPTION_MAX (MAP_MAX_SECTIONS * (MAP_CAPTION_MAX + 24))

// Fit the bank's sections into one line, preserving a split section's part.
// Empty means there is no named heading: show the bank number alone.
void desk_pager_bank_caption(const struct desk_model *model, struct font *font,
                             int page, int bank, int width,
                             char out[DESK_PAGER_BANK_CAPTION_MAX]);

#endif
