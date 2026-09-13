#include "api.h"
#include "desk_input.h"
#include "desk_master_track.h"
#include "desk_setup_layout.h"
#include "desk_speed_paint.h"

// Fresh-contact routing from dmxdesk.c, without its I/O side effects. Each
// model is reset after a probe. Capture alone is insufficient for holds and
// disabled speed targets. T_OUTSIDE is an explicit modal dismissal, not dead.
int audit_reach(struct audit_context *a,int x,int y,int *value) {
    struct desk_model *m=&a->model;
    int kind=-1,index=-1,sub=0;
    *value=-1;
    if(desk_rect_contains(desk_view_gear(),x,y))kind=A_GEAR,index=0;
    else if(a->setup.open && x<SETUP_SHEET_W) {
        struct desk_setup s=a->setup;
        struct setup_action act=desk_setup_touch_down(&s,x,y);
        if(s.capture==T_OUTSIDE)return -2;
        // Setup records arrow capture even when paging is impossible. Check
        // the release's actual transition before calling that region reachable.
        if(s.capture==T_WIFI_PREV||s.capture==T_WIFI_NEXT||s.capture==T_MASTER_PREV||s.capture==T_MASTER_NEXT) {
            enum setup_target captured=s.capture;
            int scan_page=s.scan_page,found_page=s.found_page;
            desk_setup_touch_up(&s,x,y);
            if(s.scan_page==scan_page&&s.found_page==found_page)return -1;
            kind=A_SETUP;index=captured;sub=-1;
        }
        else if(s.capture==T_KEYBOARD)kind=A_KEY,index=s.kb.pressed;
        else if(s.capture!=T_NONE)kind=A_SETUP,index=s.capture,sub=s.capture_index;
        if(act.kind==SETUP_BRIGHTNESS)*value=act.value;
    } else {
        int ci=desk_control_at(m,x,y);
        const struct desk_placement *p=ci>=0?desk_placement_of(m,ci):NULL;
        if(ci>=0 && (m->control[ci].kind==DESK_HOLD || m->control[ci].kind==DESK_BURST)) {
            struct desk_control *c=&m->control[ci];
            if(c->enabled && c->hold_index>=0) {
                struct desk_hold h=a->hold;
                if(desk_hold_press(&h,c->hold_index,0,10000).widget_id>=0)kind=A_CONTROL,index=ci;
            }
        } else if(ci>=0 && m->control[ci].kind==DESK_TEMPO) {
            struct desk_rect tap={0};
            if(p)desk_speed_tempo_tap_rect(p->x,p->y,p->w,p->h,&tap.x,&tap.y,&tap.w,&tap.h);
            if(m->control[ci].enabled && desk_rect_contains(tap,x,y) && desk_speed_target_enabled(&a->speed,0,SPEED_T_TAP)) {
                struct desk_speed s=a->speed;
                desk_speed_tap(&s,0,10000,10000);
                if(s.tap[0].last_ms==10000)kind=A_CONTROL,index=ci;
            }
        } else {
            int at;
            enum desk_target t=desk_input_target(m,x,y,&at);
            if(t==TARGET_LOCK)kind=A_LOCK,index=0;
            else if(t==TARGET_RAIL)kind=A_TAB,index=at;
            else if(t==TARGET_BANK) {
                // Detector mutation: reproduce the historical 16 px strip
                // while retaining the declared pager obligation to the edge.
                if(a->inject_pager_dead_strip && y>=DESK_CONTENT_END)return -1;
                kind=A_BANK;index=at;
            }
            else if(t==TARGET_CONTENT) {
                struct desk_rect track=desk_master_track(p,a->fonts.value);
                struct desk_action action=desk_touch_down(m,0,x,y,&track);
                if(m->capture_index>=0) {
                    kind=A_CONTROL;index=m->capture_index;
                    if(action.kind==DESK_ACT_MASTER)*value=action.value;
                    desk_touch_cancel(m,0);
                } else if(m->page==m->layout.speed_page) {
                    struct desk_speed s=a->speed;
                    desk_speed_touch_down(&s,x,y,10000,10000);
                    if(s.capture!=SPEED_T_NONE && desk_speed_target_enabled(&a->speed,s.capture_dial,s.capture))
                        kind=A_SPEED,index=s.capture,sub=s.capture_dial;
                }
            }
        }
    }
    if(index<0)return -1;
    for(int i=0;i<a->elements;i++) {
        struct audit_element *e=&a->element[i];
        if((int)e->kind==kind && e->index==index && e->sub==sub && e->enabled)return i;
    }
    // A new routed target without an inventory entry must not disappear into
    // the background classification. Disabled entries intentionally do not act.
    for(int i=0;i<a->elements;i++) {
        const struct audit_element *e=&a->element[i];
        if((int)e->kind==kind&&e->index==index&&e->sub==sub)return -1;
    }
    return -3;
}
