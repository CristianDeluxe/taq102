#include "desk_setup_row_hit.h"

struct desk_rect desk_setup_row_hit(int card_x, int row) {
    // The complete 56 px row owns the 8 px side and 4 px vertical gutters.
    return (struct desk_rect){card_x, SETUP_ROWS_Y + row * SETUP_ROW_H,
                              SETUP_CARD_W, SETUP_ROW_H};
}
