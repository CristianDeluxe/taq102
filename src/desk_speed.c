#include "desk_speed.h"

#include <stdio.h>
#include <string.h>

#include "desk_speed_layout.h"
#include "speed_factor.h"

static struct speed_action none(void) {
    struct speed_action a;
    memset(&a, 0, sizeof a);
    a.widget_id = -1;
    a.widget_id2 = -1;
    return a;
}

static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void desk_speed_init(struct desk_speed *s, const struct show_map *map) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < map->dials && i < MAP_MAX_DIALS; i++) {
        struct desk_dial *d = &s->dial[i];
        snprintf(d->caption, sizeof d->caption, "%s", map->dial[i].caption);
        d->widget_id = map->dial[i].widget_id;
        d->members = map->dial[i].members;
        d->max_ms = SPEED_MAX_MS;
        snprintf(d->reason, sizeof d->reason, "waiting for the master");
        s->dials++;
    }
    for (int i = 0; i <= MAP_MAX_DIALS; i++)
        desk_tap_init(&s->tap[i]);
    s->dirty = 1;
}

void desk_speed_validate(struct desk_speed *s, const struct vc_doc *console) {
    for (int i = 0; i < s->dials; i++) {
        struct desk_dial *d = &s->dial[i];
        const struct vc_widget *w = vc_find(console, d->widget_id);
        d->pending = 0;
        if (!w || w->type_id != VC_SPEED_DIAL) {
            d->enabled = 0;
            d->known = 0;
            snprintf(d->reason, sizeof d->reason, "not in this show");
            continue;
        }
        d->enabled = 1;
        d->max_ms = w->speed_max_ms > 0 && w->speed_max_ms < SPEED_MAX_MS ? w->speed_max_ms : SPEED_MAX_MS;
        if (w->speed_ms >= 0 && speed_factor_valid(w->speed_factor)) {
            d->base_ms = w->speed_ms;
            d->factor = w->speed_factor;
            d->known = 1;
            d->reason[0] = '\0';
        } else {
            d->known = 0;
            snprintf(d->reason, sizeof d->reason, "waiting for the master");
        }
    }
    s->dirty = 1;
}

void desk_speed_disable(struct desk_speed *s, const char *reason) {
    for (int i = 0; i < s->dials; i++) {
        struct desk_dial *d = &s->dial[i];
        d->enabled = 0;
        d->known = 0;
        d->pending = 0;
        snprintf(d->reason, sizeof d->reason, "%s", reason);
    }
    s->dirty = 1;
}

int desk_speed_bpm(const struct desk_dial *d) {
    return d->base_ms > 0 ? (60000 + d->base_ms / 2) / d->base_ms : 0;
}

static int ms_for_bpm(int bpm) {
    return bpm > 0 ? (60000 + bpm / 2) / bpm : 0;
}

static int editable(const struct desk_dial *d) {
    return d->enabled && d->known && !d->pending;
}

int desk_speed_target_enabled(const struct desk_speed *s, int dial, enum speed_target t) {
    if (t == SPEED_T_BOTH) {
        if (s->dials < 2)
            return 0;
        for (int i = 0; i < s->dials; i++)
            if (!editable(&s->dial[i]))
                return 0;
        return 1;
    }
    if (dial < 0 || dial >= s->dials)
        return 0;
    const struct desk_dial *d = &s->dial[dial];
    if (!editable(d))
        return 0;
    int bpm = desk_speed_bpm(d);
    switch (t) {
    case SPEED_T_TAP:
        return 1;
    // The step is live only when the time it would ask for is one the desk
    // sends: the same bounds time_action applies, so no live step is a no-op.
    case SPEED_T_BPM_DOWN: {
        int ms = ms_for_bpm(bpm - 1);
        return d->base_ms > 0 && bpm > 1 && ms >= SPEED_MIN_MS && ms <= d->max_ms && ms != d->base_ms;
    }
    case SPEED_T_BPM_UP: {
        int ms = ms_for_bpm(bpm + 1);
        return d->base_ms > 0 && ms >= SPEED_MIN_MS && ms <= d->max_ms && ms != d->base_ms;
    }
    // A factor change at base zero writes zeroes into every member field the
    // dial owns, so the factor targets wait for a time.
    case SPEED_T_FACTOR_ONE:
        return d->base_ms > 0 && d->factor != SPEED_FACTOR_ONE;
    case SPEED_T_HALF:
        return d->base_ms > 0 && d->factor > SPEED_FACTOR_MIN && d->factor <= SPEED_FACTOR_MAX;
    case SPEED_T_DOUBLE:
        return d->base_ms > 0 && d->factor >= SPEED_FACTOR_MIN && d->factor < SPEED_FACTOR_MAX;
    default:
        return 0;
    }
}

enum speed_target desk_speed_hit(const struct desk_speed *s, int x, int y, int *dial) {
    *dial = -1;
    if (s->dials >= 2 && inside(x, y, SPEED_CARD_X, SPEED_BOTH_Y, SPEED_CARD_W, SPEED_BOTH_H))
        return SPEED_T_BOTH;
    for (int i = 0; i < s->dials && i < 2; i++) {
        int cx = SPEED_CARD_X, cy = SPEED_CARD_Y(i);
        if (!inside(x, y, cx, cy, SPEED_CARD_W, SPEED_CARD_H))
            continue;
        *dial = i;
        if (inside(x, y, cx + SPEED_TAP_X, cy + SPEED_TAP_Y, SPEED_TAP_W, SPEED_TAP_H))
            return SPEED_T_TAP;
        static const enum speed_target grid[2][3] = {
            { SPEED_T_BPM_DOWN, SPEED_T_BPM_UP, SPEED_T_FACTOR_ONE },
            { SPEED_T_HALF, SPEED_T_DOUBLE, SPEED_T_NONE },
        };
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++)
                if (inside(x, y, cx + SPEED_CELL_X(c), cy + SPEED_CELL_Y(r), SPEED_CELL_W, SPEED_CELL_H))
                    return grid[r][c];
        return SPEED_T_NONE;
    }
    return SPEED_T_NONE;
}

static void mark_pending(struct desk_dial *d, int ms, int factor, int64_t now_ms) {
    d->pending = 1;
    d->pending_ms = ms;
    d->pending_factor = factor;
    d->pending_since = now_ms;
}

// A time for one dial, if it differs from what the master has.
static int time_action(struct desk_speed *s, int dial, int ms, int64_t now_ms, struct speed_action *a) {
    struct desk_dial *d = &s->dial[dial];
    if (ms < SPEED_MIN_MS || ms > d->max_ms || ms == d->base_ms)
        return 0;
    a->kind = SPEED_ACT_TIME;
    a->widget_id = d->widget_id;
    a->ms = ms;
    mark_pending(d, ms, d->factor, now_ms);
    s->dirty = 1;
    return 1;
}

static struct speed_action fire(struct desk_speed *s, enum speed_target t, int dial, int64_t now_ms) {
    struct speed_action a = none();
    if (!desk_speed_target_enabled(s, dial, t))
        return a;
    if (t == SPEED_T_BOTH) {
        int interval = desk_tap_down(&s->tap[MAP_MAX_DIALS], now_ms);
        if (interval <= 0)
            return a;
        // Up to two frames: a dial already at the interval gets none.
        struct speed_action first = none(), second = none();
        int sent = time_action(s, 0, interval, now_ms, &first);
        int sent2 = time_action(s, 1, interval, now_ms, &second);
        if (!sent && !sent2)
            return a;
        a.kind = SPEED_ACT_TIME_BOTH;
        a.widget_id = sent ? first.widget_id : second.widget_id;
        a.ms = interval;
        a.widget_id2 = sent && sent2 ? second.widget_id : -1;
        a.ms2 = interval;
        return a;
    }
    struct desk_dial *d = &s->dial[dial];
    int bpm = desk_speed_bpm(d);
    switch (t) {
    case SPEED_T_TAP: {
        int interval = desk_tap_down(&s->tap[dial], now_ms);
        if (interval > 0)
            time_action(s, dial, interval, now_ms, &a);
        return a;
    }
    case SPEED_T_BPM_DOWN:
        time_action(s, dial, ms_for_bpm(bpm - 1), now_ms, &a);
        return a;
    case SPEED_T_BPM_UP:
        time_action(s, dial, ms_for_bpm(bpm + 1), now_ms, &a);
        return a;
    case SPEED_T_FACTOR_ONE:
    case SPEED_T_HALF:
    case SPEED_T_DOUBLE: {
        int factor = t == SPEED_T_FACTOR_ONE ? SPEED_FACTOR_ONE
                   : t == SPEED_T_HALF ? d->factor - 1 : d->factor + 1;
        a.kind = SPEED_ACT_FACTOR;
        a.widget_id = d->widget_id;
        a.factor = factor;
        mark_pending(d, d->base_ms, factor, now_ms);
        s->dirty = 1;
        return a;
    }
    default:
        return a;
    }
}

struct speed_action desk_speed_touch_down(struct desk_speed *s, int x, int y, int64_t now_ms) {
    if (s->capture != SPEED_T_NONE)
        return none();
    int dial;
    enum speed_target t = desk_speed_hit(s, x, y, &dial);
    if (t == SPEED_T_NONE)
        return none();
    s->capture = t;
    s->capture_dial = dial;
    s->dirty = 1;
    // Tempo is the down edge: a tap fires now and its release is consumed.
    if (t == SPEED_T_TAP || t == SPEED_T_BOTH)
        return fire(s, t, dial, now_ms);
    return none();
}

struct speed_action desk_speed_touch_up(struct desk_speed *s, int x, int y, int64_t now_ms) {
    if (s->capture == SPEED_T_NONE)
        return none();
    enum speed_target was = s->capture;
    int was_dial = s->capture_dial;
    s->capture = SPEED_T_NONE;
    s->dirty = 1;
    if (was == SPEED_T_TAP || was == SPEED_T_BOTH)
        return none();
    int dial;
    if (desk_speed_hit(s, x, y, &dial) != was || dial != was_dial)
        return none();
    return fire(s, was, dial, now_ms);
}

void desk_speed_touch_cancel(struct desk_speed *s) {
    if (s->capture != SPEED_T_NONE)
        s->dirty = 1;
    s->capture = SPEED_T_NONE;
}

static void note(struct desk_dial *d, const char *text, int64_t now_ms) {
    snprintf(d->note, sizeof d->note, "%s", text);
    d->note_until = now_ms + SPEED_NOTE_MS;
}

void desk_speed_apply(struct desk_speed *s, int widget_id, int ms, int factor, int64_t now_ms) {
    if (!speed_factor_valid(factor) || ms < 0)
        return;
    for (int i = 0; i < s->dials; i++) {
        struct desk_dial *d = &s->dial[i];
        if (d->widget_id != widget_id || !d->enabled)
            continue;
        int expected = d->pending && d->pending_ms == ms && d->pending_factor == factor;
        d->pending = 0;
        d->base_ms = ms;
        d->factor = factor;
        d->known = 1;
        d->reason[0] = '\0';
        // The broadcast names no sender: an unexpected state is "updated",
        // never "the Mac's", since a late echo of our own would be mislabelled.
        if (!expected)
            note(d, "State updated", now_ms);
        s->dirty = 1;
    }
}

void desk_speed_set_link(struct desk_speed *s, int ready, int64_t now_ms) {
    (void)now_ms;
    if (s->link_ready == ready)
        return;
    s->link_ready = ready;
    if (!ready) {
        for (int i = 0; i < s->dials; i++) {
            struct desk_dial *d = &s->dial[i];
            d->known = 0;
            d->pending = 0;
            if (d->enabled)
                snprintf(d->reason, sizeof d->reason, "no link");
        }
        desk_speed_reset_taps(s);
    }
    s->dirty = 1;
}

void desk_speed_tick(struct desk_speed *s, int64_t now_ms) {
    for (int i = 0; i < s->dials; i++) {
        struct desk_dial *d = &s->dial[i];
        if (d->pending && now_ms - d->pending_since > SPEED_PENDING_MS) {
            // The master said nothing: the dial is unconfirmed until a fresh
            // snapshot or a push says where it stands. Never replayed.
            d->pending = 0;
            d->known = 0;
            snprintf(d->reason, sizeof d->reason, "no answer from the master");
            note(d, "No answer", now_ms);
            s->refresh_wanted = 1;
            s->dirty = 1;
        }
        if (d->note[0] && now_ms >= d->note_until) {
            d->note[0] = '\0';
            s->dirty = 1;
        }
    }
}

void desk_speed_reset_taps(struct desk_speed *s) {
    for (int i = 0; i <= MAP_MAX_DIALS; i++)
        desk_tap_reset(&s->tap[i]);
}
