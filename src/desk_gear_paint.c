#include "desk_gear_paint.h"

void desk_gear_paint(struct canvas *c, int x, int y, int w, int h, uint32_t ink, uint32_t hole) {
    int size = w < h ? w : h;
    int r = size * 3 / 8;
    int cx = x + w / 2, cy = y + h / 2;
    // Teeth: four rectangles through the centre, straight and diagonal.
    int tooth = r / 3;
    canvas_fill_rect(c, cx - tooth, cy - r - tooth / 2, 2 * tooth, 2 * r + tooth, ink);
    canvas_fill_rect(c, cx - r - tooth / 2, cy - tooth, 2 * r + tooth, 2 * tooth, ink);
    for (int d = -r; d <= r; d++) {
        canvas_fill_rect(c, cx + d - tooth / 2, cy + d - tooth / 2, tooth, tooth, ink);
        canvas_fill_rect(c, cx + d - tooth / 2, cy - d - tooth / 2, tooth, tooth, ink);
    }
    // The ring over the teeth, then the hole.
    canvas_round_rect(c, cx - r, cy - r, 2 * r, 2 * r, r, ink);
    canvas_round_rect(c, cx - r / 2, cy - r / 2, r, r, r / 2, hole);
}
