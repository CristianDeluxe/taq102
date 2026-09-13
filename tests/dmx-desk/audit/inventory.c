#include "api.h"
#include "desk_pager_hit.h"
#include "desk_speed_layout.h"
#include "desk_speed_paint.h"
#include "desk_setup_layout.h"

void audit_inventory(struct audit_context *a) {
    a->elements=0;
    struct desk_model *m=&a->model;
    struct desk_rect r=desk_view_gear();
    audit_add(a,A_GEAR,0,0,1,"Gear",r,r,r);
    r=desk_view_lock_target();
    audit_add(a,A_LOCK,0,0,1,"Lock",r,r,r);
    // Painted tabs remain inventoried, but setup explicitly disables navigation.
    for(int i=0;i<m->layout.pages;i++) {
        r=desk_view_tab(i);
        audit_add(a,A_TAB,i,0,!m->setup_open,m->layout.title[i],r,r,r);
    }
    for(int i=0;i<m->layout.placements;i++) {
        const struct desk_placement *p=&m->layout.placement[i];
        if(p->page!=m->page || p->bank!=m->bank)continue;
        if(a->setup.open && p->x<SETUP_SHEET_W)continue;
        if(p->control<0 || p->control>=m->count) {
            audit_note(a,0,"backing","placement %d references invalid control %d",i,p->control);
            continue;
        }
        const struct desk_control *c=&m->control[p->control];
        r=(struct desk_rect){p->x,p->y,p->w,p->h};
        if(c->kind==DESK_TEMPO)
            desk_speed_tempo_tap_rect(p->x,p->y,p->w,p->h,&r.x,&r.y,&r.w,&r.h);
        audit_add(a,A_CONTROL,p->control,0,c->enabled,c->label,r,r,r);
    }
    if(a->setup.open) { audit_setup_inventory(a); return; }
    if(m->layout.banks[m->page]>1)for(int i=0;i<m->layout.banks[m->page];i++) {
        char label[128];
        snprintf(label,sizeof label,"Bank %d",i+1);
        r=desk_view_pager(i,&m->layout,m->page);
        // The production helper explicitly assigns all glass below the pager
        // to it. Never infer this allowance from observed successful probes.
        struct desk_rect hit=desk_pager_hit(i,&m->layout,m->page);
        audit_add(a,A_BANK,i,0,1,label,r,hit,hit);
    }
    if(m->page==m->layout.speed_page) {
        for(int i=0;i<a->speed.dials;i++)for(int t=SPEED_T_TAP;t<=SPEED_T_DOUBLE;t++) {
            int cy=SPEED_CARD_Y(i), row=0,col=0;
            const char *label="Tap";
            switch(t) {
            case SPEED_T_BPM_DOWN:label="-1 BPM";break;
            case SPEED_T_BPM_UP:label="+1 BPM";col=1;break;
            case SPEED_T_FACTOR_ONE:label="Time x1";col=2;break;
            case SPEED_T_HALF:label="Slower x2";row=1;break;
            case SPEED_T_DOUBLE:label="Faster x2";row=1;col=1;break;
            default:break;
            }
            r=t==SPEED_T_TAP ? (struct desk_rect){SPEED_CARD_X+SPEED_TAP_X,cy+SPEED_TAP_Y,SPEED_TAP_W,SPEED_TAP_H}
                :(struct desk_rect){SPEED_CARD_X+SPEED_CELL_X(col),cy+SPEED_CELL_Y(row),SPEED_CELL_W,SPEED_CELL_H};
            char name[128];snprintf(name,sizeof name,"%s / %s",a->speed.dial[i].caption,label);
            audit_add(a,A_SPEED,t,i,desk_speed_target_enabled(&a->speed,i,t),name,r,r,r);
        }
        if(a->speed.dials>=2) {
            r=(struct desk_rect){SPEED_CARD_X,SPEED_BOTH_Y,SPEED_CARD_W,SPEED_BOTH_H};
            audit_add(a,A_SPEED,SPEED_T_BOTH,-1,desk_speed_target_enabled(&a->speed,0,SPEED_T_BOTH),"Tap both",r,r,r);
        }
    }
}
