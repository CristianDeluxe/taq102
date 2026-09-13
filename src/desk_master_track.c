#include "desk_master_track.h"

struct desk_rect desk_master_track(const struct desk_placement *p, struct font *value) {
    if (!p)
        return (struct desk_rect){0};
    int bh = (value ? font_height(value) : 15) + 8;
    int inset = 30 + bh + 12;
    return (struct desk_rect){ p->x + 16, p->y + inset, p->w - 32,
                               p->h - inset - 16 };
}
