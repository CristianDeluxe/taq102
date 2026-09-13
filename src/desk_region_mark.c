#include "desk_region_mark.h"

void desk_region_mark(unsigned char *mask, struct desk_rect r, int inert) {
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            if (x >= 0 && x < DESK_W && y >= 0 && y < DESK_H)
                mask[y * DESK_W + x] = inert;
}
