#ifndef AUDIT_DRAW_H
#define AUDIT_DRAW_H
#include "desk_view.h"
struct audit_draw {
    struct desk_rect rect;
    char label[192];
    unsigned char *ink;
    int text;
};
#endif
