#include "desk_speed_inert_mask.h"

void desk_speed_inert_mask(const struct desk_speed *s, unsigned char *mask) {
    // The dial readouts and card backgrounds are inert; tap and step cells
    // alone act. Disabled factor endpoints are intentionally inert too.
    for (int i = 0; i < s->dials; i++) {
        int cy = SPEED_CARD_Y(i);
        if (desk_speed_target_enabled(s, i, SPEED_T_TAP))
            desk_region_mark(mask, (struct desk_rect){SPEED_CARD_X + SPEED_TAP_X, cy + SPEED_TAP_Y, SPEED_TAP_W, SPEED_TAP_H}, 0);
        for (int t = SPEED_T_BPM_DOWN; t <= SPEED_T_DOUBLE; t++) {
            int row = t >= SPEED_T_HALF;
            int col = t == SPEED_T_BPM_UP || t == SPEED_T_DOUBLE ? 1 : t == SPEED_T_FACTOR_ONE ? 2 : 0;
            if (desk_speed_target_enabled(s, i, t))
                desk_region_mark(mask, (struct desk_rect){SPEED_CARD_X + SPEED_CELL_X(col), cy + SPEED_CELL_Y(row), SPEED_CELL_W, SPEED_CELL_H}, 0);
        }
    }
    if (s->dials >= 2 && desk_speed_target_enabled(s, 0, SPEED_T_BOTH))
        desk_region_mark(mask, (struct desk_rect){SPEED_CARD_X, SPEED_BOTH_Y, SPEED_CARD_W, SPEED_BOTH_H}, 0);
}
