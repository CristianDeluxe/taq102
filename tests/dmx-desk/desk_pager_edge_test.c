// SOURCES: desk_pager_hit.c desk_view.c desk_view_pager.c desk_pager_label.c desk_input.c desk_model.c desk_layout_resolve.c desk_show_layout.c showmap.c
// The pager must catch a finger aimed at its lower edge. These are real
// coordinates, captured from the tablet's own digitizer on 2026-09-13 while
// I pressed the bank bars the way he would at a gig, and converted
// with the transform the desk actually applies (raw 1663x895 scaled onto
// 1024x600, no flip: dmxdesk never calls touch_input_set_flipped).
//
// Before the hit rectangle was extended to the glass, the "bottom edge" rows
// below were not bank presses at all: desk_input_target answered
// TARGET_CONTENT for them, so the release margin never came into play and the
// bar did nothing.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_input.h"
#include "desk_layout.h"
#include "desk_pager_hit.h"
#include "desk_model.h"
#include "desk_view.h"

// One page of two banks, which is the COLOR page I was pressing.
static void build(struct desk_model *m) {
    desk_init(m);
    struct desk_layout layout;
    memset(&layout, 0, sizeof layout);
    layout.pages = 1;
    layout.banks[0] = 2;
    desk_set_layout(m, &layout);
}

int main(void) {
    struct desk_model m;
    build(&m);

    // The drawn strip still ends where it is painted, so nothing moved on
    // screen: only the rectangle a finger is measured against grew.
    struct desk_rect drawn = desk_view_pager(0, &m.layout, 0);
    struct desk_rect hit = desk_pager_hit(0, &m.layout, 0);
    assert(drawn.y == DESK_PAGER_Y && drawn.h == DESK_PAGER_H);
    assert(hit.x == drawn.x && hit.w == drawn.w && hit.y == drawn.y);
    assert(hit.y + hit.h == DESK_H);
    assert(hit.h > drawn.h);

    // raw x,y from the capture; screen x,y as the desk computes them.
    struct sample { int x, y; int bank; const char *what; };
    static const struct sample taps[] = {
        // Aimed normally at the bars: these already worked.
        { 123, 541, 0, "bar 1, normal" },
        { 143, 546, 0, "bar 1, normal" },
        { 596, 545, 1, "bar 2, normal" },
        { 650, 553, 1, "bar 2, normal" },
        // Aimed at the upper half: these already worked.
        { 123, 533, 0, "bar 1, upper half" },
        { 217, 536, 0, "bar 1, upper half" },
        { 566, 535, 1, "bar 2, upper half" },
        { 651, 538, 1, "bar 2, upper half" },
        // Aimed at the lower edge, against the bezel. Every one of these was
        // lost before the rectangle reached the glass.
        {  88, 587, 0, "bar 1, lower edge" },
        { 155, 593, 0, "bar 1, lower edge" },
        { 215, 590, 0, "bar 1, lower edge" },
        { 277, 592, 0, "bar 1, lower edge" },
        { 538, 593, 1, "bar 2, lower edge" },
        { 566, 594, 1, "bar 2, lower edge" },
        { 609, 596, 1, "bar 2, lower edge" },
        { 685, 596, 1, "bar 2, lower edge" },
    };

    for (size_t i = 0; i < sizeof taps / sizeof *taps; i++) {
        const struct sample *s = &taps[i];
        int index = -1;
        enum desk_target t = desk_input_target(&m, s->x, s->y, &index);
        if (t != TARGET_BANK || index != s->bank) {
            fprintf(stderr, "%s (%d,%d): target %d index %d, wanted bank %d\n",
                    s->what, s->x, s->y, (int)t, index, s->bank);
            return 1;
        }
    }

    // A press below the strip but outside every segment still belongs to
    // nothing: widening the bars must not swallow the gap between them.
    int index = -1;
    struct desk_rect a = desk_view_pager(0, &m.layout, 0);
    struct desk_rect b = desk_view_pager(1, &m.layout, 0);
    if (b.x > a.x + a.w) {
        enum desk_target t = desk_input_target(&m, a.x + a.w + 1, DESK_H - 2, &index);
        assert(t != TARGET_BANK);
    }

    // And a page with one bank has no pager at all, so the margin under the
    // content stays content's.
    struct desk_layout single;
    memset(&single, 0, sizeof single);
    single.pages = 1;
    single.banks[0] = 1;
    desk_set_layout(&m, &single);
    enum desk_target t = desk_input_target(&m, 200, DESK_H - 2, &index);
    assert(t != TARGET_BANK);

    printf("pager edge ok: %zu real taps, all reach their bank\n",
           sizeof taps / sizeof *taps);
    return 0;
}
