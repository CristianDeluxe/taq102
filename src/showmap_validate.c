#include "showmap_validate.h"

#include <stdio.h>
#include <string.h>


static void disable(struct desk_control *c, const char *reason) {
    c->enabled = 0;
    snprintf(c->reason, sizeof c->reason, "%s", reason);
}

// Whether the widget sits inside the frame, at any depth: a pick in an inner
// multipage frame still belongs to the solo frame around it.
static int under_frame(const struct vc_doc *doc, const struct vc_widget *w, int frame_id) {
    int cur = w->parent_id;
    for (int depth = 0; depth < 16 && cur >= 0; depth++) {
        if (cur == frame_id)
            return 1;
        const struct vc_widget *p = vc_find(doc, cur);
        if (!p || p->parent_id == cur)
            break;
        cur = p->parent_id;
    }
    return 0;
}

// A cue may be pressed only when the console really has that widget, it really
// is a button, it really toggles, it really drives the function the map
// claims, and it sits in the solo frame the map says it does. Anything else
// and the tile is drawn but dead.
// A hit fired while the finger is down: its widget must be a Flash button
// of this show. Fog stays on the Mac until a finite burst is proven there:
// a tablet's cap cannot bound an output after the link is lost.
static const struct vc_widget *check_hold(struct desk_control *c,
                                          const struct map_control *m,
                                          const struct vc_doc *doc) {
    const struct vc_widget *w = vc_find(doc, m->widget_id);
    if (!w) {
        disable(c, "not in this show");
        return NULL;
    }
    if (w->type_id != VC_BUTTON || w->action_type != VC_FLASH) {
        disable(c, "not a flash button");
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

// Fog and strobes stay on the Mac: a tablet's cap cannot bound an output
// after the link is lost, and neither may be left running unattended.
static int is_fog(const struct map_control *m) {
    return strncmp(m->key, "humo", 4) == 0 || strncmp(m->key, "strobo", 6) == 0;
}

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
        disable(c, w->action_type == VC_FLASH ? "held, not safe here"
                                              : "wrong button action");
        return NULL;
    }
    if (m->function_id >= 0 && w->function_id != m->function_id) {
        disable(c, "drives another cue");
        return NULL;
    }
    if (m->solo_id >= 0 && !under_frame(doc, w, m->solo_id)) {
        disable(c, "outside its frame");
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

// The console must be the one the map was generated for: the same schema on
// this side and the same QLC+ line on the master's. A mismatch disables the
// whole desk, because a map read against another show cannot be reasoned
// about, only misread.
int showmap_mismatch(const struct show_map *map, const struct vc_doc *doc) {
    if (map->schema != 2)
        return 1;
    if (doc->app_version[0] && strcmp(doc->app_version, map->qlc_version) != 0)
        return 1;
    return 0;
}

int showmap_build(struct desk_model *model, const struct show_map *map,
                  const struct vc_doc *doc) {
    desk_init(model);
    snprintf(model->master_name, sizeof model->master_name, "%s", map->key);
    int wrong_show = showmap_mismatch(map, doc);
    model->mismatch = wrong_show;

    int enabled = 0;
    for (int i = 0; i < map->count; i++) {
        const struct map_control *m = &map->control[i];
        struct desk_control c;
        memset(&c, 0, sizeof c);
        c.kind = DESK_CUE;
        snprintf(c.label, sizeof c.label, "%s", m->caption);
        snprintf(c.detail, sizeof c.detail, "%s", m->detail);
        c.widget_id = m->widget_id;
        c.function_id = m->function_id;
        c.page = m->page;
        c.section = m->section;
        c.role = m->role;
        for (int s = 0; s < m->swatches; s++)
            c.swatch[s] = 0xFF000000u | m->swatch[s];
        c.swatches = m->swatches;

        const struct vc_widget *widget = NULL;
        if (wrong_show)
            disable(&c, "show mismatch");
        else if (m->held && !is_fog(m)) {
            c.kind = DESK_HOLD;
            widget = check_hold(&c, m, doc);
        } else if (!m->enabled)
            disable(&c, m->reason[0] ? m->reason : "held on the Mac");
        else
            widget = check_cue(&c, m, doc);
        c.hold_index = -1;

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
        if (!c.enabled && !wrong_show)
            fprintf(stderr, "desk: %s: %s\n", c.label, c.reason);
        enabled += c.enabled;
    }

    struct desk_control master;
    memset(&master, 0, sizeof master);
    master.kind = DESK_MASTER;
    snprintf(master.label, sizeof master.label, "Master");
    master.widget_id = map->grand_master_widget;
    master.function_id = -1;
    master.page = -1;
    master.enabled = !wrong_show;
    if (wrong_show)
        disable(&master, "show mismatch");
    // The console's own word on the level: the snapshot carries the slider,
    // so the fader is known from the first frame rather than from the first
    // time somebody moves it on the Mac.
    const struct vc_widget *gm = wrong_show ? NULL : vc_find(doc, map->grand_master_widget);
    int gm_known = gm && gm->type_id == VC_SLIDER;
    master.level = gm_known ? gm->value : 0;
    master.requested_level = master.level;
    int master_index = desk_add(model, &master);
    if (master_index >= 0) {
        enabled += master.enabled;
        // desk_add starts every control unknown; the console's word comes after.
        if (gm_known)
            model->control[master_index].state = DESK_ON;
    }

    // SHOW's two pseudo controls, always present after the panic button so
    // their indices are known: ambient OFF, and the tempo card for the first
    // dial. Each is disabled when the show has nothing for it.
    struct desk_control off;
    memset(&off, 0, sizeof off);
    off.kind = DESK_HAZE_OFF;
    snprintf(off.label, sizeof off.label, "OFF");
    off.widget_id = -1;
    off.function_id = -1;
    off.page = -1;
    off.role = MAP_ROLE_HAZE;
    off.enabled = !wrong_show;
    if (wrong_show)
        disable(&off, "show mismatch");
    struct desk_control tempo;
    memset(&tempo, 0, sizeof tempo);
    tempo.kind = DESK_TEMPO;
    snprintf(tempo.label, sizeof tempo.label, "%s", map->dials > 0 ? map->dial[0].caption : "Tempo");
    tempo.widget_id = map->dials > 0 ? map->dial[0].widget_id : -1;
    tempo.function_id = -1;
    tempo.page = -1;
    tempo.enabled = !wrong_show && map->dials > 0;
    if (!tempo.enabled)
        disable(&tempo, wrong_show ? "show mismatch" : "no dial in this show");

    // The panic button: the console's own StopAll, which stops every function
    // whatever started it and never lies about a state of its own.
    struct desk_control stop;
    memset(&stop, 0, sizeof stop);
    stop.kind = DESK_STOP_ALL;
    snprintf(stop.label, sizeof stop.label, "PARAR TODO");
    if (map->stop_all_fade_ms > 0)
        snprintf(stop.detail, sizeof stop.detail, "%d.%d s fade",
                 map->stop_all_fade_ms / 1000, (map->stop_all_fade_ms % 1000) / 100);
    stop.widget_id = map->stop_all_widget;
    stop.function_id = -1;
    stop.page = -1;
    const struct vc_widget *w = map->stop_all_widget >= 0 ? vc_find(doc, map->stop_all_widget) : NULL;
    if (wrong_show)
        disable(&stop, "show mismatch");
    else if (!w)
        disable(&stop, "not in this show");
    else if (w->type_id != VC_BUTTON || w->action_type != VC_STOP_ALL)
        disable(&stop, "not a stop-all button");
    else
        stop.enabled = 1;
    if (desk_add(model, &stop) >= 0)
        enabled += stop.enabled;
    if (desk_add(model, &off) < 0 || desk_add(model, &tempo) < 0)
        fprintf(stderr, "desk: no room for SHOW's own controls\n");
    else
        enabled += off.enabled + tempo.enabled;
    return enabled;
}
