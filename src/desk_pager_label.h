#ifndef DESK_PAGER_LABEL_H
#define DESK_PAGER_LABEL_H

#include "desk_layout_resolve.h"

// The first heading supplies the font-independent geometry's width hint.
// Display labels are composed separately by desk_pager_bank_caption.
const char *desk_pager_label(const struct desk_layout *layout, int page, int bank);

#endif
