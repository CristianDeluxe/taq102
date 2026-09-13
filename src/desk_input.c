#include "desk_input.h"

#include <string.h>

#include "desk_layout.h"
#include "desk_pager_hit.h"
#include "desk_view.h"

enum desk_target desk_input_target(const struct desk_model *m, int x, int y, int *index) {
    *index = -1;
    if (x < 0 || x >= DESK_W || y < 0 || y >= DESK_H)
        return TARGET_NONE;
    if (y < DESK_BAR_H) {
        if (desk_rect_contains(desk_view_lock_target(), x, y))
            return TARGET_LOCK;
        // Setup is modal over navigation; the master, gear and lock remain.
        for (int i = 0; !m->setup_open && i < m->layout.pages; i++) {
            if (desk_rect_contains(desk_view_tab(i), x, y)) {
                *index = i;
                return TARGET_RAIL;
            }
        }
        return TARGET_NONE;     // the gear is the caller's
    }
    if (x >= DESK_MASTER_X)
        return TARGET_CONTENT;
    int banks = m->layout.pages > 0 ? m->layout.banks[m->page] : 1;
    if (banks > 1 && y >= DESK_PAGER_Y) {
        for (int i = 0; i < banks; i++) {
            if (desk_rect_contains(desk_pager_hit(i, &m->layout, m->page), x, y)) {
                *index = i;
                return TARGET_BANK;
            }
        }
    }
    return TARGET_CONTENT;
}

void desk_input_init(struct desk_input *in) {
    memset(in, 0, sizeof *in);
    for (int i = 0; i < TOUCH_MAX_SLOTS; i++)
        in->slot[i].index = -1;
}

void desk_input_cancel_all(struct desk_input *in) {
    desk_input_init(in);
}

enum desk_target desk_input_feed(struct desk_input *in, const struct desk_model *m,
                                 const struct touch_event *ev, int *index) {
    *index = -1;
    if (ev->slot < 0 || ev->slot >= TOUCH_MAX_SLOTS)
        return TARGET_NONE;
    int at;
    if (ev->kind == TOUCH_DOWN) {
        if (ev->x < 0 || ev->x >= DESK_W || ev->y < 0 || ev->y >= DESK_H) {
            in->slot[ev->slot].target = TARGET_NONE;
            in->slot[ev->slot].index = -1;
            return TARGET_NONE;
        }
        in->slot[ev->slot].target = desk_input_target(m, (int)ev->x, (int)ev->y, &at);
        in->slot[ev->slot].index = at;
        in->slot[ev->slot].page = m->page;
        // The rectangle the release margin is measured against has to be the
        // one the press was accepted in, or a tap that lands in the pager's
        // margin is taken and then dropped on the lift.
        in->slot[ev->slot].rect = in->slot[ev->slot].target == TARGET_RAIL
                               ? desk_view_tab(at) : desk_pager_hit(at, &m->layout, m->page);
        return in->slot[ev->slot].target;
    }
    enum desk_target owner = in->slot[ev->slot].target;
    int pressed = in->slot[ev->slot].index;
    if (ev->kind == TOUCH_UP || ev->kind == TOUCH_CANCEL) {
        in->slot[ev->slot].target = TARGET_NONE;
        in->slot[ev->slot].index = -1;
    }
    if ((owner == TARGET_RAIL || owner == TARGET_BANK) && ev->kind == TOUCH_UP) {
        if (ev->x < 0 || ev->x >= DESK_W || ev->y < 0 || ev->y >= DESK_H)
            return owner;
        enum desk_target now = desk_input_target(m, (int)ev->x, (int)ev->y, &at);
        // A neighbouring entry wins hit testing but never inherits this tap.
        // Only the originally pressed rectangle gets the finger-roll margin.
        if ((now == TARGET_RAIL || now == TARGET_BANK) && (now != owner || at != pressed))
            return owner;
        if (owner == TARGET_BANK && m->page != in->slot[ev->slot].page)
            return owner;
        struct desk_rect r = in->slot[ev->slot].rect;
        r.x -= DESK_INPUT_SLOP;
        r.y -= DESK_INPUT_SLOP;
        r.w += 2 * DESK_INPUT_SLOP;
        r.h += 2 * DESK_INPUT_SLOP;
        if (pressed >= 0 && desk_rect_contains(r, (int)ev->x, (int)ev->y))
            *index = pressed;
    }
    return owner;
}
