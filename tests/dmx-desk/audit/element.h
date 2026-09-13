#ifndef AUDIT_ELEMENT_H
#define AUDIT_ELEMENT_H
#include "desk_view.h"
struct audit_element {
    char label[128];
    enum { A_CONTROL, A_TAB, A_BANK, A_GEAR, A_LOCK, A_SPEED, A_SETUP, A_KEY } kind;
    int index, sub, enabled;
    struct desk_rect drawn, hit, allowance;
    int painted, reachable, min_x, min_y, max_x, max_y, slop_count, slop_x, slop_y;
};
#endif
