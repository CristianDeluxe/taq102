#include "touch_router.h"

#include <stdlib.h>
#include <string.h>

#define PANEL_OUTPUT_CAPACITY 32
#define OPEN_DRAG_DISTANCE 60.f
#define CLOSE_DRAG_DISTANCE 60.f
#define HANDLE_WIDTH 36
#define HANDLE_HEIGHT 5
#define HANDLE_TOP 10

enum contact_owner {
    OWNER_NONE,
    OWNER_CANDIDATE,
    OWNER_CUBE,
    OWNER_PANEL,
    OWNER_PANEL_IGNORED,
};

struct routed_contact {
    enum contact_owner owner;
    float start_y;
    int opening_triggered;
    int dismiss_on_release;
    int handle_drag;
    int closing_triggered;
};

struct touch_router {
    int bar_h;
    int panel_open;
    int panel_x;
    int panel_y;
    int panel_w;
    int panel_h;
    int panel_primary;
    struct routed_contact contact[TOUCH_MAX_SLOTS];
    struct cube_contacts cube;
    struct touch_event *pending;
    size_t pending_length;
    size_t pending_capacity;
    int pending_error;
};

static int point_in_panel(const struct touch_router *router, float x, float y) {
    return x >= router->panel_x && x < router->panel_x + router->panel_w &&
           y >= router->panel_y && y < router->panel_y + router->panel_h;
}

static int point_in_handle(const struct touch_router *router, float x, float y) {
    int x0 = router->panel_x + (router->panel_w - HANDLE_WIDTH) / 2;
    int y0 = router->panel_y + HANDLE_TOP;
    return x >= x0 && x < x0 + HANDLE_WIDTH && y >= y0 &&
           y < y0 + HANDLE_HEIGHT;
}

static int append_panel(struct touch_router *router,
                        const struct touch_event *event) {
    if (router->pending_length == router->pending_capacity) {
        size_t capacity = router->pending_capacity ? router->pending_capacity * 2 : 32;
        struct touch_event *pending =
            realloc(router->pending, capacity * sizeof(*pending));
        if (!pending) {
            router->pending_error = 1;
            return -1;
        }
        router->pending = pending;
        router->pending_capacity = capacity;
    }
    router->pending[router->pending_length++] = *event;
    return 0;
}

static void clear_contact(struct touch_router *router, int slot) {
    if (router->panel_primary == slot) router->panel_primary = -1;
    memset(&router->contact[slot], 0, sizeof(router->contact[slot]));
}

static void begin_contact(struct touch_router *router,
                          const struct touch_event *event) {
    int slot = event->slot;
    clear_contact(router, slot);
    struct routed_contact *contact = &router->contact[slot];
    contact->start_y = event->y;
    if (router->panel_open) {
        if (router->panel_primary >= 0) {
            contact->owner = OWNER_PANEL_IGNORED;
            return;
        }
        contact->owner = OWNER_PANEL;
        router->panel_primary = slot;
        contact->dismiss_on_release = !point_in_panel(router, event->x, event->y);
        contact->handle_drag = point_in_handle(router, event->x, event->y);
        (void)append_panel(router, event);
        return;
    }
    if (event->y < router->bar_h) {
        contact->owner = OWNER_CANDIDATE;
        return;
    }
    contact->owner = OWNER_CUBE;
    router->cube.slot[slot].active = 1;
    router->cube.slot[slot].x = event->x;
    router->cube.slot[slot].y = event->y;
}

static void route_candidate(struct touch_router *router,
                            const struct touch_event *event,
                            struct router_out *out) {
    struct routed_contact *contact = &router->contact[event->slot];
    if (event->kind == TOUCH_MOVE && !contact->opening_triggered &&
        event->y - contact->start_y >= OPEN_DRAG_DISTANCE) {
        contact->opening_triggered = 1;
        router->panel_open = 1;
        router->panel_primary = event->slot;
        out->open_panel = 1;
    }
    if (event->kind == TOUCH_UP || event->kind == TOUCH_CANCEL)
        clear_contact(router, event->slot);
}

static void route_cube(struct touch_router *router,
                       const struct touch_event *event) {
    int slot = event->slot;
    if (event->kind == TOUCH_MOVE) {
        router->cube.slot[slot].x = event->x;
        router->cube.slot[slot].y = event->y;
    } else if (event->kind == TOUCH_UP || event->kind == TOUCH_CANCEL) {
        router->cube.slot[slot].active = 0;
        clear_contact(router, slot);
    }
}

static void route_panel(struct touch_router *router,
                        const struct touch_event *event,
                        struct router_out *out) {
    struct routed_contact *contact = &router->contact[event->slot];
    (void)append_panel(router, event);
    if (event->kind == TOUCH_MOVE && contact->handle_drag &&
        !contact->closing_triggered &&
        contact->start_y - event->y >= CLOSE_DRAG_DISTANCE) {
        contact->closing_triggered = 1;
        out->close_panel = 1;
    }
    if (event->kind == TOUCH_UP) {
        if (contact->dismiss_on_release) out->close_panel = 1;
        clear_contact(router, event->slot);
    } else if (event->kind == TOUCH_CANCEL) {
        clear_contact(router, event->slot);
    }
}

static void route_ignored(struct touch_router *router,
                          const struct touch_event *event) {
    if (event->kind == TOUCH_UP || event->kind == TOUCH_CANCEL)
        clear_contact(router, event->slot);
}

static void drain_panel(struct touch_router *router, struct router_out *out) {
    if (router->pending_error) {
        out->n_panel = -1;
        return;
    }
    size_t count = router->pending_length;
    if (count > PANEL_OUTPUT_CAPACITY) count = PANEL_OUTPUT_CAPACITY;
    if (count > 0) {
        memcpy(out->panel, router->pending, count * sizeof(*out->panel));
        router->pending_length -= count;
        memmove(router->pending, router->pending + count,
                router->pending_length * sizeof(*router->pending));
    }
    out->n_panel = (int)count;
}

struct touch_router *touch_router_new(int w, int h, int bar_h, int panel_x,
                                      int panel_y, int panel_w, int panel_h) {
    if (w <= 0 || h <= 0 || bar_h < 0 || panel_w <= 0 || panel_h <= 0)
        return NULL;
    struct touch_router *router = calloc(1, sizeof(*router));
    if (!router) return NULL;
    router->bar_h = bar_h;
    router->panel_x = panel_x;
    router->panel_y = panel_y;
    router->panel_w = panel_w;
    router->panel_h = panel_h;
    router->panel_primary = -1;
    return router;
}

void touch_router_free(struct touch_router *router) {
    if (!router) return;
    free(router->pending);
    free(router);
}

void touch_router_set_panel_open(struct touch_router *router, int open) {
    if (router) router->panel_open = !!open;
}

void touch_router_feed(struct touch_router *router,
                       const struct touch_event *event, int count,
                       struct router_out *out) {
    if (!router || !out) return;
    memset(out, 0, sizeof(*out));
    if (count < 0 || (count > 0 && !event)) {
        out->n_panel = -1;
        return;
    }
    for (int i = 0; i < count; ++i) {
        const struct touch_event *current = &event[i];
        if (current->slot < 0 || current->slot >= TOUCH_MAX_SLOTS) continue;
        if (current->kind == TOUCH_DOWN) {
            begin_contact(router, current);
            continue;
        }
        switch (router->contact[current->slot].owner) {
        case OWNER_CANDIDATE:
            route_candidate(router, current, out);
            break;
        case OWNER_CUBE:
            route_cube(router, current);
            break;
        case OWNER_PANEL:
            route_panel(router, current, out);
            break;
        case OWNER_PANEL_IGNORED:
            route_ignored(router, current);
            break;
        case OWNER_NONE:
            break;
        }
    }
    out->cube = router->cube;
    drain_panel(router, out);
}

void touch_router_reset(struct touch_router *router) {
    if (!router) return;
    memset(router->contact, 0, sizeof(router->contact));
    memset(&router->cube, 0, sizeof(router->cube));
    router->panel_primary = -1;
    router->pending_length = 0;
    router->pending_error = 0;
}
