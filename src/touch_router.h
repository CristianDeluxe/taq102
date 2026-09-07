#ifndef TOUCH_ROUTER_H
#define TOUCH_ROUTER_H

#include "touch_input.h"

struct cube_contacts {
    struct {
        int active;
        float x;
        float y;
    } slot[TOUCH_MAX_SLOTS];
};

struct router_out {
    int open_panel;
    int close_panel;
    int n_panel;
    struct touch_event panel[32];
    struct cube_contacts cube;
};

struct touch_router;

struct touch_router *touch_router_new(int w, int h, int bar_h, int panel_x,
                                      int panel_y, int panel_w, int panel_h);
void touch_router_free(struct touch_router *router);
void touch_router_set_panel_open(struct touch_router *router, int open);
/* Pass count == 0 to drain panel events retained beyond panel[32]. */
void touch_router_feed(struct touch_router *router,
                       const struct touch_event *event, int count,
                       struct router_out *out);
void touch_router_reset(struct touch_router *router);

#endif
