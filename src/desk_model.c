#include "desk_model.h"

#include <string.h>

#include "desk_layout.h"

#define NO_CAPTURE (-1)

static struct desk_action none(void) {
    struct desk_action a = { DESK_ACT_NONE, -1, 0 };
    return a;
}

static int inside(const struct desk_control *c, int x, int y) {
    return c->w > 0 && c->h > 0 &&
           x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h;
}

static int hit(const struct desk_model *m, int x, int y) {
    for (int i = 0; i < m->count; i++) {
        if (inside(&m->control[i], x, y))
            return i;
    }
    return -1;
}

// The master's fill runs bottom to top, and a drag anywhere in the tile sets
// it. 8 is the floor: a master that reads zero looks like a dead rig.
static int master_level_at(const struct desk_control *c, int y) {
    int span = c->h > 1 ? c->h - 1 : 1;
    int from_bottom = c->y + c->h - 1 - y;
    if (from_bottom < 0)
        from_bottom = 0;
    if (from_bottom > span)
        from_bottom = span;
    return from_bottom * 255 / span;
}

void desk_init(struct desk_model *m) {
    memset(m, 0, sizeof *m);
    m->link = DESK_LINK_DOWN;
    m->dirty = 1;
    m->capture_slot = NO_CAPTURE;
    m->capture_index = -1;
}

int desk_add(struct desk_model *m, const struct desk_control *control) {
    if (m->count >= DESK_MAX_CONTROLS)
        return -1;
    m->control[m->count] = *control;
    m->control[m->count].state = DESK_UNKNOWN;
    m->control[m->count].pressed = 0;
    m->dirty = 1;
    return m->count++;
}

static int usable(const struct desk_model *m, const struct desk_control *c) {
    return c->enabled && m->link == DESK_LINK_READY;
}

struct desk_action desk_touch_down(struct desk_model *m, int slot, int x, int y) {
    if (m->capture_slot != NO_CAPTURE)
        return none();
    int index = hit(m, x, y);
    if (index < 0)
        return none();
    struct desk_control *c = &m->control[index];
    if (!usable(m, c))
        return none();

    m->capture_slot = slot;
    m->capture_index = index;
    c->pressed = 1;
    m->dirty = 1;

    // A fader follows the finger from the first contact; a cue waits for the
    // release, so sliding off it is a way to change your mind.
    if (c->kind == DESK_MASTER) {
        c->requested_level = master_level_at(c, y);
        struct desk_action a = { DESK_ACT_MASTER, c->widget_id, c->requested_level };
        return a;
    }
    return none();
}

struct desk_action desk_touch_move(struct desk_model *m, int slot, int x, int y) {
    if (slot != m->capture_slot || m->capture_index < 0)
        return none();
    struct desk_control *c = &m->control[m->capture_index];
    if (c->kind == DESK_MASTER) {
        int level = master_level_at(c, y);
        if (level != c->requested_level) {
            c->requested_level = level;
            m->dirty = 1;
            struct desk_action a = { DESK_ACT_MASTER, c->widget_id, level };
            return a;
        }
        return none();
    }
    int was = c->pressed;
    c->pressed = inside(c, x, y);
    if (c->pressed != was)
        m->dirty = 1;
    return none();
}

struct desk_action desk_touch_up(struct desk_model *m, int slot, int x, int y) {
    if (slot != m->capture_slot || m->capture_index < 0)
        return none();
    struct desk_control *c = &m->control[m->capture_index];
    int fired = (c->kind == DESK_CUE || c->kind == DESK_STOP_ALL) && inside(c, x, y) &&
                usable(m, c);
    c->pressed = 0;
    m->capture_slot = NO_CAPTURE;
    m->capture_index = -1;
    m->dirty = 1;
    if (!fired)
        return none();
    // One gesture, one message. The tile does not change colour here: the
    // master's own push is what lights it. The panic button is the same
    // shape of gesture, a completed tap, with nothing to light afterwards.
    struct desk_action a = { c->kind == DESK_STOP_ALL ? DESK_ACT_STOP_ALL : DESK_ACT_TOGGLE,
                             c->widget_id, 255 };
    return a;
}

void desk_touch_cancel(struct desk_model *m, int slot) {
    if (slot != m->capture_slot || m->capture_index < 0)
        return;
    m->control[m->capture_index].pressed = 0;
    m->capture_slot = NO_CAPTURE;
    m->capture_index = -1;
    m->dirty = 1;
}

void desk_apply_function(struct desk_model *m, int function_id, int running) {
    for (int i = 0; i < m->count; i++) {
        struct desk_control *c = &m->control[i];
        if (c->kind == DESK_CUE && c->function_id == function_id) {
            enum desk_state state = running ? DESK_ON : DESK_OFF;
            if (c->state != state) {
                c->state = state;
                m->dirty = 1;
            }
        }
    }
}

// The first push makes the level known whatever its value: a master that
// happens to sit where the desk guessed is still a master that has spoken.
// A finger on the tile keeps its own request; the fill follows the push.
void desk_apply_master(struct desk_model *m, int value) {
    if (value < 0 || value > 255)
        return;
    for (int i = 0; i < m->count; i++) {
        struct desk_control *c = &m->control[i];
        if (c->kind != DESK_MASTER)
            continue;
        c->level = value;
        c->state = DESK_ON;
        if (m->capture_index != i)
            c->requested_level = value;
        m->dirty = 1;
    }
}

void desk_set_link(struct desk_model *m, enum desk_link link) {
    if (m->link == link)
        return;
    m->link = link;
    if (link != DESK_LINK_READY) {
        for (int i = 0; i < m->count; i++) {
            m->control[i].state = DESK_UNKNOWN;
            m->control[i].pressed = 0;
        }
        m->capture_slot = NO_CAPTURE;
        m->capture_index = -1;
    }
    m->dirty = 1;
}
