#include "desk_pager_bank_caption.h"

#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "desk_pager_caption.h"

void desk_pager_bank_caption(const struct desk_model *model, struct font *font,
                             int page, int bank, int width,
                             char out[DESK_PAGER_BANK_CAPTION_MAX]) {
    const struct desk_layout *layout = &model->layout;
    const struct desk_heading *preferred = NULL;
    int headings = 0, largest = -1, overflow = 0;
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < layout->headings; i++) {
        const struct desk_heading *h = &layout->heading[i];
        if (h->page != page || h->bank != bank || !h->text[0])
            continue;
        int area = 0;
        for (int j = 0; j < layout->placements; j++) {
            const struct desk_placement *p = &layout->placement[j];
            if (p->page == page && p->bank == bank && p->tile != TILE_MASTER &&
                p->tile != TILE_PANIC && p->control >= 0 && p->control < model->count &&
                model->control[p->control].section == h->section)
                area += p->w * p->h;
        }
        if (area > largest) {
            largest = area;
            preferred = h;
        }
        char part[24] = "";
        if (h->parts > 1)
            snprintf(part, sizeof part, " %d/%d", h->part, h->parts);
        const char *join = headings ? (font ? " \xc2\xb7 " : " + ") : "";
        int n = snprintf(out + used, DESK_PAGER_BANK_CAPTION_MAX - used,
                         "%s%s%s", join, h->text, part);
        if (n < 0 || (size_t)n >= DESK_PAGER_BANK_CAPTION_MAX - used)
            overflow = 1;
        else
            used += (size_t)n;
        headings++;
    }
    if (!preferred || (!overflow &&
        (font ? font_width(font, out) : canvas_text_width(out, 3)) <= width))
        return;

    // Join complete headings when they fit. Otherwise name the section whose
    // tiles occupy the most area on this bank (first heading wins ties), not
    // a small section that merely starts it. +N acknowledges the other sections.
    // Reserve the part first: it distinguishes banks of the same section and
    // must survive even when the font fallback leaves room for no name at all.
    char part[24] = "", suffix[48];
    if (preferred->parts > 1)
        snprintf(part, sizeof part, " %d/%d", preferred->part, preferred->parts);
    if (headings > 1)
        snprintf(suffix, sizeof suffix, "%s +%d", part, headings - 1);
    else
        snprintf(suffix, sizeof suffix, "%s", part);
    if ((font ? font_width(font, suffix + (suffix[0] == ' ')) :
                canvas_text_width(suffix + (suffix[0] == ' '), 3)) > width)
        snprintf(suffix, sizeof suffix, "%s", part);
    int budget = width - (font ? font_width(font, suffix) : canvas_text_width(suffix, 3));
    do {
        char name[MAP_CAPTION_MAX + 4];
        desk_pager_caption(font, preferred->text, budget, name);
        snprintf(out, DESK_PAGER_BANK_CAPTION_MAX, "%s%s", name,
                 suffix + (!name[0] && suffix[0] == ' '));
        if ((font ? font_width(font, out) : canvas_text_width(out, 3)) <= width)
            return;
        budget--;
    } while (budget >= 0);
}
