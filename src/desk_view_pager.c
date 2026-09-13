#include "desk_view.h"

#include "desk_layout.h"
#include "desk_layout_resolve.h"
#include "desk_pager_label.h"

// The minimum width has to leave the widest case some spare, or the division
// by `total` below is a division by zero rather than a squeeze: six segments
// and their gaps must fit the content with room left over.
_Static_assert(DESK_MAX_BANKS * DESK_PAGER_MIN_W + (DESK_MAX_BANKS - 1) * DESK_GAP
                   < DESK_CONTENT_W,
               "the pager's minimum width leaves no spare at DESK_MAX_BANKS");

// Spend the whole strip on targets. Labels get a preferred ten pixels per
// UTF-8 character plus room for the number and padding; actual glyphs are
// measured and ellipsized by the painter. This keeps hit geometry independent
// of font loading. Every segment keeps 128 px even when six labels are long.
struct desk_rect desk_view_pager(int index, const struct desk_layout *layout, int page) {
    struct desk_rect none = {0};
    if (page < 0 || page >= layout->pages)
        return none;
    int banks = layout->banks[page];
    if (banks < 2 || banks > DESK_MAX_BANKS || index < 0 || index >= banks)
        return none;
    int extra[DESK_MAX_BANKS], total = 0;
    for (int i = 0; i < banks; i++) {
        const unsigned char *label = (const unsigned char *)desk_pager_label(layout, page, i);
        int chars = 0;
        for (; *label; label++)
            if ((*label & 0xC0) != 0x80)
                chars++;
        int wanted = 64 + chars * 10;
        extra[i] = wanted > DESK_PAGER_MIN_W ? wanted - DESK_PAGER_MIN_W : 0;
        total += extra[i];
    }
    int available = DESK_CONTENT_W - (banks - 1) * DESK_GAP;
    int spare = available - banks * DESK_PAGER_MIN_W;
    int before = 0;
    for (int i = 0; i < index; i++)
        before += extra[i];
    int left, right;
    if (total > spare) {
        left = before * spare / total;
        right = (before + extra[index]) * spare / total;
    } else {
        // Labels that fit share the remaining space, rather than leaving a
        // large dead area beside two tiny buttons at the edge of the glass.
        left = before + index * (spare - total) / banks;
        right = before + extra[index] + (index + 1) * (spare - total) / banks;
    }
    struct desk_rect r = { DESK_CONTENT_X + index * (DESK_PAGER_MIN_W + DESK_GAP) + left,
                           DESK_PAGER_Y, DESK_PAGER_MIN_W + right - left, DESK_PAGER_H };
    return r;
}
