#include "desk_setup_page_hit.h"

struct desk_rect desk_setup_page_hit(int card_x, int next) {
    // Both painted arrow envelopes and hits are 48x48. The extra height
    // uses the empty top of the current-status row without moving the list.
    return (struct desk_rect){card_x + SETUP_CARD_W - 2 * SETUP_PAGE_W - 8
                              + next * SETUP_PAGE_W,
                              SETUP_CARD_Y, SETUP_PAGE_W, SETUP_PAGE_H};
}
