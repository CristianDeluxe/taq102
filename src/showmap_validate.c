#include "showmap_validate.h"

#include <stdio.h>
#include <string.h>

#include "desk_layout.h"

static void slot_rect(int row, int col, struct desk_control *out) {
    out->w = DESK_TILE_W;
    out->h = DESK_TILE_H;
    out->x = DESK_GRID_X + col * (DESK_TILE_W + DESK_GAP);
    out->y = DESK_GRID_Y + row * (DESK_TILE_H + DESK_GAP);
}

static void disable(struct desk_control *c, const char *reason) {
    c->enabled = 0;
    snprintf(c->reason, sizeof c->reason, "%s", reason);
}

// A cue may be pressed only when the console really has that widget, it really
// is a button, it really toggles, and it really drives the function the map
// claims. Anything else and the tile is drawn but dead.
// Returns the widget when the control may be pressed, NULL when it may not.
static const struct vc_widget *check_cue(struct desk_control *c,
                                         const struct map_control *m,
                                         const struct vc_doc *doc) {
    const struct vc_widget *w = vc_find(doc, m->widget_id);
    if (!w) {
        disable(c, "not in this show");
        return NULL;
    }
    if (w->type_id != VC_BUTTON) {
        disable(c, "not a button");
        return NULL;
    }
    if (w->action_type != VC_TOGGLE) {
        // Flash is the one that cannot be made safe over a network.
        disable(c, w->action_type == VC_FLASH ? "flash, not safe here"
                                              : "wrong button action");
        return NULL;
    }
    if (m->function_id >= 0 && w->function_id != m->function_id) {
        disable(c, "drives another cue");
        return NULL;
    }
    if (w->disabled) {
        disable(c, "disabled on the master");
        return NULL;
    }
    c->function_id = w->function_id;
    c->enabled = 1;
    c->reason[0] = '\0';
    return w;
}

int showmap_build(struct desk_model *model, const struct show_map *map,
                  const struct vc_doc *doc) {
    desk_init(model);
    snprintf(model->master_name, sizeof model->master_name, "%s", map->key);

    int enabled = 0;
    for (int i = 0; i < map->count; i++) {
        const struct vc_widget *widget = NULL;
        const struct map_control *m = &map->control[i];
        struct desk_control c;
        memset(&c, 0, sizeof c);
        snprintf(c.label, sizeof c.label, "%s", m->label);
        c.widget_id = m->widget_id;
        c.function_id = m->function_id;

        switch (m->action) {
        case MAP_TOGGLE:
            c.kind = DESK_CUE;
            slot_rect(m->row, m->col, &c);
            widget = check_cue(&c, m, doc);
            break;
        case MAP_MASTER:
            c.kind = DESK_MASTER;
            c.x = DESK_MASTER_TILE_X;
            c.y = DESK_MASTER_TILE_Y;
            c.w = DESK_MASTER_TILE_W;
            c.h = DESK_MASTER_TILE_H;
            c.enabled = 1;
            // Unknown until the master says; 0 here is never painted.
            c.level = 0;
            c.requested_level = 0;
            break;
        case MAP_BLACKOUT:
            c.kind = DESK_BLACKOUT;
            c.x = DESK_MASTER_TILE_X;
            c.y = DESK_BLACKOUT_Y;
            c.w = DESK_MASTER_TILE_W;
            c.h = DESK_BLACKOUT_H;
            // Deliberate: QLC+'s blackout widget reports whatever the last
            // sender claimed, so there is no state this desk can trust yet.
            disable(&c, "no trusted state");
            break;
        }
        int index = desk_add(model, &c);
        if (index < 0) {
            fprintf(stderr, "desk: more controls than the model holds\n");
            break;
        }
        // The console's document carries each widget's state as it was when
        // the desk fetched it. That is the only snapshot there is: opening the
        // socket sends nothing, so without this every tile would start
        // unknown and stay unknown until someone touched the show.
        if (widget)
            model->control[index].state = widget->state ? DESK_ON : DESK_OFF;
        enabled += c.enabled;
    }
    return enabled;
}
