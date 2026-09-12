#include "desk_model.h"

#include <string.h>

#define NO_CAPTURE (-1)

static struct desk_action none(void) {
    struct desk_action a = { DESK_ACT_NONE, -1, 0 };
    return a;
}

static int inside(const struct desk_placement *p, int x, int y) {
    return p->w > 0 && p->h > 0 &&
           x >= p->x && x < p->x + p->w && y >= p->y && y < p->y + p->h;
}

static int on_view(const struct desk_model *m, const struct desk_placement *p) {
    return p->page == m->page && p->bank == m->bank;
}

// The placement under a point on the current view, or -1.
static int hit(const struct desk_model *m, int x, int y) {
    for (int i = 0; i < m->layout.placements; i++) {
        const struct desk_placement *p = &m->layout.placement[i];
        if (on_view(m, p) && inside(p, x, y))
            return i;
    }
    return -1;
}

// The master's fill runs bottom to top, and a drag anywhere in the tile sets
// it.
static int master_level_at(const struct desk_placement *p, int y) {
    int span = p->h > 1 ? p->h - 1 : 1;
    int from_bottom = p->y + p->h - 1 - y;
    if (from_bottom < 0)
        from_bottom = 0;
    if (from_bottom > span)
        from_bottom = span;
    return from_bottom * 255 / span;
}

static void damage_all(struct desk_model *m) {
    m->damage_all = 1;
    m->dirty = 1;
}

void desk_damage_rect(struct desk_model *m, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return;
    m->dirty = 1;
    if (m->damage_all)
        return;
    if (m->damage_w <= 0) {
        m->damage_x = x; m->damage_y = y; m->damage_w = w; m->damage_h = h;
        return;
    }
    int x0 = m->damage_x < x ? m->damage_x : x;
    int y0 = m->damage_y < y ? m->damage_y : y;
    int x1 = m->damage_x + m->damage_w > x + w ? m->damage_x + m->damage_w : x + w;
    int y1 = m->damage_y + m->damage_h > y + h ? m->damage_y + m->damage_h : y + h;
    m->damage_x = x0; m->damage_y = y0; m->damage_w = x1 - x0; m->damage_h = y1 - y0;
}

// A control's every placement on the current view joins the damage.
static void damage_control(struct desk_model *m, int control) {
    for (int i = 0; i < m->layout.placements; i++) {
        const struct desk_placement *p = &m->layout.placement[i];
        if (on_view(m, p) && p->control == control)
            desk_damage_rect(m, p->x, p->y, p->w, p->h);
    }
}

int desk_take_damage(struct desk_model *m, int *x, int *y, int *w, int *h) {
    if (!m->dirty)
        return 0;
    if (m->damage_all || m->damage_w <= 0) {
        *x = 0; *y = 0; *w = -1; *h = -1;
    } else {
        *x = m->damage_x; *y = m->damage_y; *w = m->damage_w; *h = m->damage_h;
    }
    m->damage_all = 0;
    m->damage_w = 0;
    m->damage_h = 0;
    m->dirty = 0;
    return 1;
}

static void release(struct desk_model *m) {
    if (m->capture_index >= 0) {
        m->control[m->capture_index].pressed = 0;
        damage_control(m, m->capture_index);
    }
    m->capture_slot = NO_CAPTURE;
    m->capture_index = -1;
    m->capture_placement = -1;
    m->dirty = 1;
}

void desk_init(struct desk_model *m) {
    memset(m, 0, sizeof *m);
    m->link = DESK_LINK_DOWN;
    m->dirty = 1;
    m->capture_slot = NO_CAPTURE;
    m->capture_index = -1;
    m->capture_placement = -1;
    damage_all(m);
}

int desk_add(struct desk_model *m, const struct desk_control *control) {
    if (m->count >= DESK_MAX_CONTROLS)
        return -1;
    m->control[m->count] = *control;
    m->control[m->count].state = DESK_UNKNOWN;
    m->control[m->count].pressed = 0;
    damage_all(m);
    return m->count++;
}

void desk_set_layout(struct desk_model *m, const struct desk_layout *layout) {
    m->layout = *layout;
    m->page = 0;
    m->bank = 0;
    release(m);
    damage_all(m);
}

void desk_set_view(struct desk_model *m, int page, int bank) {
    if (page < 0)
        page = 0;
    if (page >= m->layout.pages)
        page = m->layout.pages > 0 ? m->layout.pages - 1 : 0;
    int banks = m->layout.banks[page] > 0 ? m->layout.banks[page] : 1;
    if (bank < 0)
        bank = 0;
    if (bank >= banks)
        bank = banks - 1;
    if (page == m->page && bank == m->bank)
        return;
    m->page = page;
    m->bank = bank;
    release(m);
    damage_all(m);
}

const struct desk_placement *desk_placement_of(const struct desk_model *m, int control) {
    for (int i = 0; i < m->layout.placements; i++) {
        const struct desk_placement *p = &m->layout.placement[i];
        if (on_view(m, p) && p->control == control)
            return p;
    }
    return NULL;
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
    const struct desk_placement *p = &m->layout.placement[index];
    struct desk_control *c = &m->control[p->control];
    if (!usable(m, c))
        return none();

    m->capture_slot = slot;
    m->capture_index = p->control;
    m->capture_placement = index;
    c->pressed = 1;
    damage_control(m, p->control);

    // A fader follows the finger from the first contact; a cue waits for the
    // release, so sliding off it is a way to change your mind.
    if (c->kind == DESK_MASTER) {
        c->requested_level = master_level_at(p, y);
        struct desk_action a = { DESK_ACT_MASTER, c->widget_id, c->requested_level };
        return a;
    }
    return none();
}

struct desk_action desk_touch_move(struct desk_model *m, int slot, int x, int y) {
    if (slot != m->capture_slot || m->capture_index < 0)
        return none();
    struct desk_control *c = &m->control[m->capture_index];
    const struct desk_placement *p = &m->layout.placement[m->capture_placement];
    if (c->kind == DESK_MASTER) {
        int level = master_level_at(p, y);
        if (level != c->requested_level) {
            c->requested_level = level;
            damage_control(m, m->capture_index);
            struct desk_action a = { DESK_ACT_MASTER, c->widget_id, level };
            return a;
        }
        return none();
    }
    int was = c->pressed;
    c->pressed = inside(p, x, y);
    if (c->pressed != was)
        damage_control(m, m->capture_index);
    return none();
}

struct desk_action desk_touch_up(struct desk_model *m, int slot, int x, int y) {
    if (slot != m->capture_slot || m->capture_index < 0)
        return none();
    struct desk_control *c = &m->control[m->capture_index];
    const struct desk_placement *p = &m->layout.placement[m->capture_placement];
    int fired = (c->kind == DESK_CUE || c->kind == DESK_STOP_ALL) && inside(p, x, y) &&
                usable(m, c);
    release(m);
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
    release(m);
}

void desk_cancel_all(struct desk_model *m) {
    if (m->capture_index >= 0)
        release(m);
}

void desk_set_locked(struct desk_model *m, int locked) {
    if (m->locked == !!locked)
        return;
    m->locked = !!locked;
    release(m);
    damage_all(m);
}

void desk_apply_function(struct desk_model *m, int function_id, int running) {
    for (int i = 0; i < m->count; i++) {
        struct desk_control *c = &m->control[i];
        if (c->kind == DESK_CUE && c->function_id == function_id) {
            enum desk_state state = running ? DESK_ON : DESK_OFF;
            if (c->state != state) {
                c->state = state;
                damage_control(m, i);
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
        damage_control(m, i);
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
        m->capture_placement = -1;
    }
    damage_all(m);
}
