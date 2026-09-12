#include "desk_input.h"

#include <string.h>

#include "desk_layout.h"
#include "desk_view.h"

enum desk_target desk_input_target(const struct desk_model *m, int x, int y, int *index) {
    *index = -1;
    if (y < DESK_BAR_H)
        return TARGET_NONE;
    if (x < DESK_RAIL_W) {
        if (desk_rect_contains(desk_view_lock_target(), x, y))
            return TARGET_LOCK;
        for (int i = 0; i < m->layout.pages; i++) {
            if (desk_rect_contains(desk_view_rail_entry(i), x, y)) {
                *index = i;
                return TARGET_RAIL;
            }
        }
        return TARGET_NONE;
    }
    int banks = m->layout.pages > 0 ? m->layout.banks[m->page] : 1;
    if (banks > 1 && x < DESK_MASTER_X && y >= DESK_H - DESK_SELECTOR_H) {
        for (int i = 0; i < banks; i++) {
            if (desk_rect_contains(desk_view_bank_button(i), x, y)) {
                *index = i;
                return TARGET_BANK;
            }
        }
        return TARGET_NONE;
    }
    return TARGET_CONTENT;
}

void desk_input_init(struct desk_input *in) {
    memset(in, 0, sizeof *in);
    for (int i = 0; i < TOUCH_MAX_SLOTS; i++)
        in->slot[i].index = -1;
}

enum desk_target desk_input_feed(struct desk_input *in, const struct desk_model *m,
                                 const struct touch_event *ev, int *index) {
    *index = -1;
    if (ev->slot < 0 || ev->slot >= TOUCH_MAX_SLOTS)
        return TARGET_NONE;
    int at;
    if (ev->kind == TOUCH_DOWN) {
        in->slot[ev->slot].target = desk_input_target(m, (int)ev->x, (int)ev->y, &at);
        in->slot[ev->slot].index = at;
        return in->slot[ev->slot].target;
    }
    enum desk_target owner = in->slot[ev->slot].target;
    int pressed = in->slot[ev->slot].index;
    if (ev->kind == TOUCH_UP || ev->kind == TOUCH_CANCEL) {
        in->slot[ev->slot].target = TARGET_NONE;
        in->slot[ev->slot].index = -1;
    }
    if ((owner == TARGET_RAIL || owner == TARGET_BANK) && ev->kind == TOUCH_UP) {
        enum desk_target now = desk_input_target(m, (int)ev->x, (int)ev->y, &at);
        if (now == owner && at == pressed)
            *index = at;
    }
    return owner;
}
