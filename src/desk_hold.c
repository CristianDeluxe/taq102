#include "desk_hold.h"

#include <string.h>

static int64_t cooldown_end(int64_t now, int duration) {
    return now > INT64_MAX - duration ? INT64_MAX : now + duration;
}

static void note_time(struct desk_hold *h, int64_t now) {
    for (int i = 0; i < h->count; i++) {
        struct desk_hold_control *c = &h->control[i];
        if (c->cooldown_until == -1)
            c->cooldown_until = cooldown_end(now, c->cooldown_ms);
    }
}

static struct hold_action off(struct desk_hold_control *c, int64_t now) {
    c->owed_release = 0;
    if (c->kind == HOLD_FOG)
        c->cooldown_until = now < 0 ? -1 : cooldown_end(now, c->cooldown_ms);
    return (struct hold_action){ c->widget_id, 0 };
}

static int drain(struct desk_hold *h, struct hold_action *out, int cap, int64_t now) {
    if (!h->link_ready || !out || cap <= 0)
        return 0;
    int n = 0;
    for (int i = 0; i < h->count && n < cap; i++) {
        struct desk_hold_control *c = &h->control[i];
        if (c->owed_release && (!c->down || c->capped))
            out[n++] = off(c, now);
    }
    return n;
}

void desk_hold_init(struct desk_hold *h) {
    memset(h, 0, sizeof *h);
    for (int i = 0; i < 16; i++) {
        h->control[i].widget_id = -1;
        h->control[i].slot = -1;
    }
}

int desk_hold_add(struct desk_hold *h, int widget_id, enum hold_kind kind,
                  int cap_ms, int cooldown_ms) {
    if (h->count >= 16 || widget_id < 0 || (kind != HOLD_HIT && kind != HOLD_FOG) ||
        cap_ms <= 0 || cooldown_ms < 0)
        return -1;
    for (int i = 0; i < h->count; i++)
        if (h->control[i].widget_id == widget_id)
            return -1;
    int i = h->count++;
    h->control[i] = (struct desk_hold_control){
        .widget_id = widget_id, .kind = kind, .cap_ms = cap_ms,
        .cooldown_ms = cooldown_ms, .slot = -1
    };
    return i;
}

struct hold_action desk_hold_press(struct desk_hold *h, int i, int slot, int64_t now) {
    struct hold_action none = { -1, 0 };
    if (i < 0 || i >= h->count || slot < 0 || now < 0)
        return none;
    note_time(h, now);
    if (!h->link_ready)
        return none;
    for (int j = 0; j < h->count; j++) {
        const struct desk_hold_control *c = &h->control[j];
        if (c->owed_release && (!c->down || c->capped))
            return none;
    }
    struct desk_hold_control *c = &h->control[i];
    if (c->down || c->capped || now < c->cooldown_until)
        return none;
    c->down = 1;
    c->slot = slot;
    c->down_since = now;
    c->owed_release = 1;
    return (struct hold_action){ c->widget_id, 1 };
}

struct hold_action desk_hold_release(struct desk_hold *h, int i, int slot, int64_t now) {
    struct hold_action none = { -1, 0 };
    if (i < 0 || i >= h->count || now < 0)
        return none;
    note_time(h, now);
    struct desk_hold_control *c = &h->control[i];
    if (!c->down || c->slot != slot)
        return none;
    c->down = 0;
    c->slot = -1;
    c->capped = 0;
    return h->link_ready && c->owed_release ? off(c, now) : none;
}

int desk_hold_release_all(struct desk_hold *h, int64_t now, struct hold_action *out, int cap) {
    if (now < 0)
        return 0;
    note_time(h, now);
    for (int i = 0; i < h->count; i++) {
        h->control[i].down = 0;
        h->control[i].slot = -1;
        h->control[i].capped = 0;
    }
    return drain(h, out, cap, now);
}

int desk_hold_tick(struct desk_hold *h, int64_t now, struct hold_action *out, int cap) {
    if (now < 0)
        return 0;
    note_time(h, now);
    for (int i = 0; i < h->count; i++) {
        struct desk_hold_control *c = &h->control[i];
        if (c->down && !c->capped && now >= c->down_since &&
            now - c->down_since >= c->cap_ms)
            c->capped = 1;
    }
    return drain(h, out, cap, now);
}

void desk_hold_set_link(struct desk_hold *h, int ready) {
    h->link_ready = !!ready;
    if (!h->link_ready)
        for (int i = 0; i < h->count; i++)
            if (h->control[i].down)
                h->control[i].capped = 1;
}

int desk_hold_owed(struct desk_hold *h, struct hold_action *out, int cap) {
    return drain(h, out, cap, -1);
}

int desk_hold_progress(const struct desk_hold *h, int i, int64_t now) {
    if (i < 0 || i >= h->count || !h->control[i].down)
        return -1;
    const struct desk_hold_control *c = &h->control[i];
    if (c->capped)
        return 1000;
    if (now <= c->down_since)
        return 0;
    int64_t elapsed = now - c->down_since;
    return elapsed >= c->cap_ms ? 1000 : (int)(elapsed * 1000 / c->cap_ms);
}
