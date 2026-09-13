#include "api.h"
#include "desk_burst_base.h"

void audit_backing(struct audit_context *a,const struct vc_doc *doc) {
    snprintf(a->view,sizeof a->view,"model / all pages and banks");
    for(int i=0;i<a->model.count;i++) {
        const struct desk_control *c=&a->model.control[i];
        if(!c->enabled)continue;
        int placements=0;
        for(int j=0;j<a->model.layout.placements;j++) {
            const struct desk_placement *p=&a->model.layout.placement[j];
            if(p->control==i&&p->page>=0&&p->page<a->model.layout.pages&&p->bank>=0&&p->bank<a->model.layout.banks[p->page])placements++;
        }
        if(!placements)audit_note(a,0,"backing","enabled %s control=%d has no valid page/bank placement",c->label,i);
        // Ambient OFF is an aggregate action over the haze cues, not a widget.
        if(c->kind!=DESK_HAZE_OFF && (c->widget_id<0||!vc_find(doc,c->widget_id)))
            audit_note(a,0,"backing","enabled %s control=%d has missing widget id=%d",c->label,i,c->widget_id);
        if(c->kind==DESK_HOLD||c->kind==DESK_BURST) {
            int slot=c->hold_index;
            int expected=c->kind==DESK_BURST?BURST_BASE+c->function_id:c->widget_id;
            if(slot<0||slot>=a->hold.count||a->hold.control[slot].widget_id!=expected)
                audit_note(a,0,"backing","enabled %s control=%d hold slot=%d / count=%d, expected action id=%d",c->label,i,slot,a->hold.count,expected);
        }
        if(c->kind==DESK_TEMPO && (a->speed.dials<1||!a->speed.dial[0].enabled||c->widget_id!=a->speed.dial[0].widget_id))
            audit_note(a,0,"backing","enabled %s lacks matching enabled speed dial",c->label);
        if(c->kind==DESK_HAZE_OFF) {
            int backed=0;
            for(int j=0;j<a->model.count;j++)if(a->model.control[j].kind==DESK_CUE&&a->model.control[j].role==MAP_ROLE_HAZE&&a->model.control[j].enabled)backed++;
            if(!backed)audit_note(a,0,"backing","enabled %s has no enabled haze cues to stop",c->label);
        }
    }
    for(int i=0;i<a->speed.dials;i++)if(a->speed.dial[i].enabled) {
        if(!vc_find(doc,a->speed.dial[i].widget_id)||a->model.layout.speed_page<0||i>=2)
            audit_note(a,0,"backing","enabled speed dial %s index=%d widget=%d has no widget or painted card",a->speed.dial[i].caption,i,a->speed.dial[i].widget_id);
    }
}
