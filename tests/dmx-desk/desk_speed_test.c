// SOURCES: desk_speed.c desk_tap.c speed_factor.c showmap.c vcjson.c
// The cards against the Vibra map and console: unknown until the snapshot,
// then 120 BPM on both; +1 sends 496 and waits; an echo of 496 is silent, an
// unexpected one is "State updated"; x2 sends factor 7; a same-value tap
// sends nothing; Tap both sends up to two; a timeout asks for a snapshot;
// a link drop forgets everything.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desk_speed.h"
#include "desk_speed_layout.h"
#include "showmap.h"
#include "vcjson.h"

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    assert(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    buf[n] = '\0';
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static struct speed_action tap(struct desk_speed *s, int x, int y, int64_t now) {
    struct speed_action down = desk_speed_touch_down(s, x, y, now, now);
    struct speed_action up = desk_speed_touch_up(s, x, y, now);
    return down.kind != SPEED_ACT_NONE ? down : up;
}

// A point inside a card's target.
static int tx(int col) { return SPEED_CARD_X + SPEED_CELL_X(col) + 10; }
static int ty(int dial, int row) { return SPEED_CARD_Y(dial) + SPEED_CELL_Y(row) + 10; }

int main(void) {
    size_t map_len, vc_len;
    char *map_text = slurp("show/vibra.desk.json", &map_len);
    char *vc_text = slurp("tests/dmx-desk/fixtures/vc-vibra.json", &vc_len);
    struct show_map map;
    assert(showmap_parse(map_text, map_len, &map) == 0 && map.dials == 2);
    struct vc_doc console;
    assert(vc_parse(vc_text, vc_len, &console) == 0);

    struct desk_speed s;
    desk_speed_init(&s, &map);
    assert(s.dials == 2 && strcmp(s.dial[0].caption, "Tempo Show") == 0);
    int64_t now = 100000;
    // Nothing is known: every target is dead, Tap both included.
    assert(!desk_speed_target_enabled(&s, 0, SPEED_T_TAP));
    assert(tap(&s, tx(1), ty(0, 0), now).kind == SPEED_ACT_NONE);

    desk_speed_validate(&s, &console);
    assert(s.dial[0].known && s.dial[0].base_ms == 500 && s.dial[0].factor == 6);
    assert(s.dial[1].known && s.dial[1].widget_id == 274 && s.dial[1].max_ms == 2000);
    assert(desk_speed_bpm(&s.dial[0]) == 120);

    // +1 BPM: 121 -> 496 ms, one frame, pending until the echo.
    struct speed_action a = tap(&s, tx(1), ty(0, 0), now);
    assert(a.kind == SPEED_ACT_TIME && a.widget_id == 34 && a.ms == 496);
    assert(s.dial[0].pending && !desk_speed_target_enabled(&s, 0, SPEED_T_BPM_UP));
    assert(tap(&s, tx(1), ty(0, 0), now).kind == SPEED_ACT_NONE);
    desk_speed_apply(&s, 34, 496, 6, now);
    assert(!s.dial[0].pending && s.dial[0].base_ms == 496 && s.dial[0].note[0] == '\0');
    assert(desk_speed_bpm(&s.dial[0]) == 121);
    // An unexpected push is adopted and noted, never called the Mac's.
    desk_speed_apply(&s, 34, 400, 6, now);
    assert(s.dial[0].base_ms == 400 && strcmp(s.dial[0].note, "State updated") == 0);
    desk_speed_tick(&s, now + SPEED_NOTE_MS + 1);
    assert(s.dial[0].note[0] == '\0');

    // x2: factor 7; x1/2 from 2 is dead; x1 is dead at 1 already.
    a = tap(&s, tx(1), ty(0, 1), now);
    assert(a.kind == SPEED_ACT_FACTOR && a.widget_id == 34 && a.factor == 7);
    desk_speed_apply(&s, 34, 400, 7, now);
    assert(!s.dial[0].pending);
    desk_speed_apply(&s, 34, 400, 2, now);
    assert(!desk_speed_target_enabled(&s, 0, SPEED_T_HALF) && desk_speed_target_enabled(&s, 0, SPEED_T_DOUBLE));
    assert(desk_speed_target_enabled(&s, 0, SPEED_T_FACTOR_ONE));
    a = tap(&s, tx(2), ty(0, 0), now);
    assert(a.kind == SPEED_ACT_FACTOR && a.factor == 6);
    desk_speed_apply(&s, 34, 400, 6, now);
    // A dial factor of None multiplies by zero in the engine: steps off, x1 is the way back.
    desk_speed_apply(&s, 34, 400, 0, now);
    assert(!desk_speed_target_enabled(&s, 0, SPEED_T_HALF) && !desk_speed_target_enabled(&s, 0, SPEED_T_DOUBLE));
    assert(desk_speed_target_enabled(&s, 0, SPEED_T_FACTOR_ONE));
    desk_speed_apply(&s, 34, 400, 6, now);
    // No base time: factor steps wait for one; tap still sets a time.
    desk_speed_apply(&s, 34, 0, 6, now);
    assert(!desk_speed_target_enabled(&s, 0, SPEED_T_FACTOR_ONE) && !desk_speed_target_enabled(&s, 0, SPEED_T_BPM_UP));
    assert(desk_speed_target_enabled(&s, 0, SPEED_T_TAP));
    desk_speed_apply(&s, 34, 500, 6, now);

    // Tap on dial 0: the first seeds, 500 ms later commits 500, which is the
    // master's value already, so nothing is sent; 400 ms commits 400 on the
    // down edge and waits for the echo.
    int tapx = SPEED_CARD_X + SPEED_TAP_X + 10, tapy0 = SPEED_CARD_Y(0) + SPEED_TAP_Y + 10;
    int64_t t = 200000;
    desk_speed_tick(&s, t);     // the earlier notes have expired
    assert(desk_speed_touch_down(&s, tapx, tapy0, t, t).kind == SPEED_ACT_NONE);
    assert(desk_speed_touch_up(&s, tapx, tapy0, t).kind == SPEED_ACT_NONE);
    assert(tap(&s, tapx, tapy0, t + 500).kind == SPEED_ACT_NONE && !s.dial[0].pending);
    desk_speed_reset_taps(&s);
    assert(tap(&s, tapx, tapy0, t + 1000).kind == SPEED_ACT_NONE);
    a = desk_speed_touch_down(&s, tapx, tapy0, t + 1400, t + 1400);
    assert(a.kind == SPEED_ACT_TIME && a.widget_id == 34 && a.ms == 400 && s.dial[0].pending);
    assert(desk_speed_touch_up(&s, tapx, tapy0, t + 1400).kind == SPEED_ACT_NONE);
    desk_speed_apply(&s, 34, 400, 6, t + 1450);
    assert(!s.dial[0].pending && s.dial[0].note[0] == '\0');

    // Tap both: up to two frames from one tap, none for a dial already there,
    // and dead while either dial waits.
    int bx = SPEED_CARD_X + 10, by = SPEED_BOTH_Y + 10;
    desk_speed_reset_taps(&s);
    t = 300000;
    assert(desk_speed_touch_down(&s, bx, by, t, t).kind == SPEED_ACT_NONE);
    desk_speed_touch_up(&s, bx, by, t);
    a = desk_speed_touch_down(&s, bx, by, t + 450, t + 450);
    desk_speed_touch_up(&s, bx, by, t + 450);
    assert(a.kind == SPEED_ACT_TIME_BOTH && a.widget_id == 34 && a.ms == 450 && a.widget_id2 == 274 && a.ms2 == 450);
    assert(s.dial[0].pending && s.dial[1].pending && !desk_speed_target_enabled(&s, 0, SPEED_T_BOTH));
    assert(tap(&s, bx, by, t + 900).kind == SPEED_ACT_NONE);
    desk_speed_apply(&s, 34, 450, 6, t + 500);
    desk_speed_apply(&s, 274, 450, 6, t + 500);
    assert(desk_speed_target_enabled(&s, 0, SPEED_T_BOTH));
    desk_speed_reset_taps(&s);
    desk_speed_apply(&s, 274, 500, 6, t + 600);
    tap(&s, bx, by, t + 1000);
    a = tap(&s, bx, by, t + 1450);
    assert(a.kind == SPEED_ACT_TIME_BOTH && a.widget_id == 274 && a.ms == 450 && a.widget_id2 == -1);
    assert(!s.dial[0].pending && s.dial[1].pending);
    desk_speed_apply(&s, 274, 450, 6, t + 1500);

    // A tap drained late keeps its interval but its deadline runs from now.
    desk_speed_reset_taps(&s);
    desk_speed_apply(&s, 34, 500, 6, t);
    desk_speed_touch_down(&s, tapx, tapy0, t + 5000, t + 3000);
    desk_speed_touch_up(&s, tapx, tapy0, t + 5000);
    a = desk_speed_touch_down(&s, tapx, tapy0, t + 5400, t + 3400);
    desk_speed_touch_up(&s, tapx, tapy0, t + 5400);
    assert(a.kind == SPEED_ACT_TIME && a.ms == 400 && s.dial[0].pending_since == t + 5400);
    desk_speed_tick(&s, t + 5400 + SPEED_PENDING_MS - 1);
    assert(s.dial[0].pending);
    desk_speed_apply(&s, 34, 400, 6, t + 5450);

    // Bounds: at 2000 ms (30 BPM) -1 is dead; at 200 ms (300 BPM) +1 is dead.
    desk_speed_apply(&s, 34, 2000, 6, t);
    assert(!desk_speed_target_enabled(&s, 0, SPEED_T_BPM_DOWN) && desk_speed_target_enabled(&s, 0, SPEED_T_BPM_UP));
    desk_speed_apply(&s, 34, 200, 6, t);
    assert(desk_speed_target_enabled(&s, 0, SPEED_T_BPM_DOWN) && !desk_speed_target_enabled(&s, 0, SPEED_T_BPM_UP));
    desk_speed_apply(&s, 34, 500, 6, t);

    // A change the master never echoes: the dial goes unconfirmed, the desk
    // asks for a snapshot, and the snapshot brings it back.
    a = tap(&s, tx(1), ty(0, 0), t);
    assert(a.kind == SPEED_ACT_TIME && s.dial[0].pending);
    desk_speed_tick(&s, t + SPEED_PENDING_MS + 1);
    assert(!s.dial[0].pending && !s.dial[0].known && s.refresh_wanted);
    assert(strcmp(s.dial[0].note, "No answer") == 0);
    assert(!desk_speed_target_enabled(&s, 0, SPEED_T_TAP));
    s.refresh_wanted = 0;
    desk_speed_validate(&s, &console);
    assert(s.dial[0].known && s.dial[0].base_ms == 500);

    // A link drop forgets everything; the link coming back knows nothing yet.
    desk_speed_set_link(&s, 1, t);
    desk_speed_set_link(&s, 0, t);
    assert(!s.dial[0].known && !s.dial[1].known && strcmp(s.dial[1].reason, "no link") == 0);
    desk_speed_set_link(&s, 1, t);
    assert(!s.dial[0].known);
    desk_speed_apply(&s, 34, 500, 6, t);
    assert(s.dial[0].known && !s.dial[1].known);

    // A dial the console does not have is disabled with its reason.
    struct show_map other = map;
    other.dial[1].widget_id = 9999;
    struct desk_speed o;
    desk_speed_init(&o, &other);
    desk_speed_validate(&o, &console);
    assert(o.dial[0].enabled && !o.dial[1].enabled && strcmp(o.dial[1].reason, "not in this show") == 0);
    assert(!desk_speed_target_enabled(&o, 0, SPEED_T_BOTH));

    vc_free(&console);
    free(map_text);
    free(vc_text);
    printf("desk_speed ok\n");
    return 0;
}
