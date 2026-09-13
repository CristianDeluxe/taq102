#include "api.h"
#include <stdlib.h>
#include "speed_factor.h"

void audit_ranges(struct audit_context *a) {
    for(int i=0;i<a->elements;i++) {
        const struct audit_element *e=&a->element[i];
        int master=e->kind==A_CONTROL&&a->model.control[e->index].kind==DESK_MASTER;
        int brightness=e->kind==A_SETUP&&e->index==T_FADER;
        if(!e->enabled||(!master&&!brightness))continue;
        // Observe the first inset track actually drawn by the real painter.
        // Full tile backgrounds and readout text cannot qualify as a track.
        struct desk_rect track={0};
        for(int j=0;j<a->draws;j++) {
            const struct audit_draw *d=&a->draw[j];
            struct desk_rect r=d->rect;
            if(d->text||r.x<=e->drawn.x||r.x+r.w>=e->drawn.x+e->drawn.w||
                r.y<=e->drawn.y||r.y+r.h>=e->drawn.y+e->drawn.h)continue;
            if((master&&r.h>e->drawn.h/2)||(brightness&&r.w>e->drawn.w/2&&r.h<e->drawn.h/2)) {track=r;break;}
        }
        if(!track.w) {audit_note(a,0,"range","%s: no drawn track found",e->label);continue;}
        int samples=master?track.h:track.w;
        int low=master?0:8,high=master?255:a->setup.brightness_max;
        int bad=0,first=-1,first_value=-1,values[3]={-1,-1,-1},previous=master?high:low;
        for(int step=0;step<samples;step++) {
            int x=master?track.x+track.w/2:track.x+step;
            int y=master?track.y+step:track.y+track.h/2;
            int value,owner=audit_reach(a,x,y,&value);
            int slot=step==0?0:step==samples-1?2:step==samples/2?1:-1;
            if(slot>=0)values[slot]=value;
            int expected=slot==0?(master?high:low):slot==2?(master?low:high):(high+low)/2;
            int fail=owner!=i||value<low||value>high||(master?value>previous:value<previous)||
                (slot>=0&&abs(value-expected)>AUDIT_RANGE_TOLERANCE);
            if(fail){if(!bad)first=step,first_value=value;bad++;}
            previous=value;
        }
        if(!bad)fprintf(a->report,"RANGE PASS %s: endpoints/midpoint=%d/%d/%d, %d monotonic painted-track probes\n",
            e->label,values[0],values[1],values[2],samples);
        if(bad)audit_note(a,0,"range","%s drawn track=(%d,%d,%d,%d), %s/mid/%s=%d/%d/%d expected=%d/%d/%d; %d bad samples, first=(%d,%d) value=%d",
            e->label,track.x,track.y,track.w,track.h,master?"top":"left",master?"bottom":"right",values[0],values[1],values[2],master?high:low,(high+low)/2,master?low:high,bad,
            master?track.x+track.w/2:track.x+first,master?track.y+first:track.y+track.h/2,first_value);
    }
    if(a->setup.open||a->model.page!=a->model.layout.speed_page)return;
    // SPEED has discrete steps/tap-time input, not a vertical fader. Exercise
    // endpoints/midpoint through actual painted tap coordinates and model actions.
    for(int dial=0;dial<a->speed.dials;dial++) {
        struct audit_element *tap=NULL;
        for(int i=0;i<a->elements;i++)if(a->element[i].kind==A_SPEED&&a->element[i].index==SPEED_T_TAP&&a->element[i].sub==dial)tap=&a->element[i];
        if(!tap||!tap->reachable)continue;
        for(int sample=0;sample<3;sample++) {
            struct desk_speed s=a->speed;
            int want=sample==0?SPEED_MIN_MS:sample==2?s.dial[dial].max_ms:(SPEED_MIN_MS+s.dial[dial].max_ms)/2;
            s.dial[dial].base_ms=want==SPEED_MIN_MS?s.dial[dial].max_ms:SPEED_MIN_MS;
            s.dial[dial].known=1;s.dial[dial].pending=0;
            desk_speed_reset_taps(&s);
            int x=tap->min_x,y=tap->min_y;
            desk_speed_touch_down(&s,x,y,10000,10000);
            desk_speed_touch_cancel(&s);
            struct speed_action action=desk_speed_touch_down(&s,x,y,10000+want,10000+want);
            if(action.kind!=SPEED_ACT_TIME||action.ms!=want||action.widget_id!=s.dial[dial].widget_id)
                audit_note(a,0,"speed-range","%s at (%d,%d): tap interval=%d expected time action, got kind=%d ms=%d widget=%d",tap->label,x,y,want,action.kind,action.ms,action.widget_id);
        }
        for(int direction=0;direction<2;direction++) {
            struct desk_speed s=a->speed;
            s.dial[dial].factor=SPEED_FACTOR_ONE;
            int target=direction?SPEED_T_DOUBLE:SPEED_T_HALF;
            int want=direction?SPEED_FACTOR_MAX:SPEED_FACTOR_MIN;
            struct audit_element *button=NULL;
            for(int i=0;i<a->elements;i++)if(a->element[i].kind==A_SPEED&&a->element[i].index==target&&a->element[i].sub==dial)button=&a->element[i];
            if(!button)continue;
            int x=button->drawn.x+button->drawn.w/2,y=button->drawn.y+button->drawn.h/2;
            for(int step=0;step<SPEED_FACTOR_MAX-SPEED_FACTOR_MIN+1;step++) {
                desk_speed_touch_down(&s,x,y,10000+step,10000+step);
                struct speed_action action=desk_speed_touch_up(&s,x,y,10000+step);
                if(action.kind!=SPEED_ACT_FACTOR)break;
                desk_speed_apply(&s,action.widget_id,s.dial[dial].base_ms,action.factor,10000+step);
            }
            if(s.dial[dial].factor!=want)audit_note(a,0,"speed-range","%s at (%d,%d): factor stopped at %d, expected %d",button->label,x,y,s.dial[dial].factor,want);
        }
    }
}
