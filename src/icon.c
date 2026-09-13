#include "icon.h"

#include "canvas_blend.h"

void icon_paint(struct canvas *c, enum icon_id id, int x, int y, uint32_t colour) {
    if (id < 0 || id >= ICON_COUNT)
        return;
    const uint8_t *a = ICON_ALPHA[id];
    for (int j = 0; j < ICON_SIZE; j++)
        for (int i = 0; i < ICON_SIZE; i++) {
            uint8_t alpha = a[j * ICON_SIZE + i];
            if (alpha)
                canvas_blend(c, x + i, y + j, ((uint32_t)alpha << 24) | (colour & 0xFFFFFF));
        }
}

void icon_paint_battery(struct canvas *c, int x, int y, int level, int charging, uint32_t ink,
                        uint32_t fill, uint32_t bolt) {
    icon_paint(c, ICON_BATTERY, x, y, ink);
    if (level < 0)
        level = 0;
    if (level > 100)
        level = 100;
    // Clip the cell's actual bitmap extent so regenerated geometry keeps its level.
    const uint8_t *a = ICON_ALPHA[ICON_BATTERY_CELL];
    int first = ICON_SIZE, last = -1;
    for (int i = 0; i < ICON_SIZE; i++)
        for (int j = 0; j < ICON_SIZE; j++)
            if (a[j * ICON_SIZE + i]) {
                if (i < first) first = i;
                if (i > last) last = i;
            }
    int span = last - first + 1;
    int lit = span * level / 100;
    for (int j = 0; j < ICON_SIZE; j++)
        for (int i = first; i < first + lit; i++) {
            uint8_t alpha = a[j * ICON_SIZE + i];
            if (alpha)
                canvas_blend(c, x + i, y + j, ((uint32_t)alpha << 24) | (fill & 0xFFFFFF));
        }
    if (charging)
        icon_paint(c, ICON_BOLT, x - 2, y, bolt);
}

void icon_paint_wifi(struct canvas *c, int x, int y, int bars, uint32_t ink, uint32_t dim) {
    icon_paint(c, ICON_WIFI_0, x, y, bars > 0 ? ink : dim);
    for (int i = 1; i <= 3; i++)
        icon_paint(c, (enum icon_id)(ICON_WIFI_0 + i), x, y, i <= bars ? ink : dim);
}
