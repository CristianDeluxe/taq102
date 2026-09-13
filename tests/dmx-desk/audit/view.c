#include "api.h"
#include <stdlib.h>
#include "desk_paint.h"
#include "desk_speed_paint.h"
#include "desk_setup_paint.h"
#include "desk_setup_layout.h"
void audit_view(struct audit_context *a,const char *state) {
    snprintf(a->view,sizeof a->view,"page=%s bank=%d state=%s",a->model.layout.title[a->model.page],a->model.bank+1,state);
    fprintf(a->report,"VIEW %s\n",a->view);fflush(a->report);
    for(int i=0;i<a->draws;i++)free(a->draw[i].ink);
    a->draws=0;
    audit_active=a;
    desk_paint(&a->canvas,&a->model,&a->fonts);
    for(int i=0;i<a->model.count;i++)if(a->model.control[i].kind==DESK_TEMPO) {
        const struct desk_placement *p=desk_placement_of(&a->model,i);
        if(p&&a->model.control[i].enabled)
            desk_speed_paint_tempo(&a->canvas,&a->speed,0,&a->fonts,p->x,p->y,p->w,p->h,0);
    }
    if(a->model.page==a->model.layout.speed_page)desk_speed_paint(&a->canvas,&a->speed,&a->fonts);
    if(a->setup.open) {
        // Opaque setup replaces content; retaining hidden primitives would
        // invent collisions. Keep the bar/master, then trace the new surface.
        int out=0;
        for(int i=0;i<a->draws;i++) {
            struct audit_draw d=a->draw[i];
            if(d.rect.y>=DESK_BAR_H&&d.rect.x<SETUP_SHEET_W)free(d.ink);
            else a->draw[out++]=d;
        }
        a->draws=out;
        desk_setup_paint(&a->canvas,&a->setup,&a->fonts);
        if(a->setup.confirm_open) {
            // The modal scrim retains the underlying visual but intentionally
            // disables it. Only dialog text participates in collision checks.
            int dialog_start=-1;
            for(int i=0;i<a->draws;i++) {
                struct desk_rect r=a->draw[i].rect;
                if(!a->draw[i].text&&r.x==SETUP_CONFIRM_X&&r.y==SETUP_CONFIRM_Y&&r.w==SETUP_CONFIRM_W&&r.h==SETUP_CONFIRM_H)dialog_start=i;
            }
            out=0;
            for(int i=0;i<a->draws;i++) {
                struct audit_draw d=a->draw[i];
                if(i<dialog_start&&d.rect.y>=DESK_BAR_H&&d.rect.x<SETUP_SHEET_W)free(d.ink);
                else a->draw[out++]=d;
            }
            a->draws=out;
        }
    }
    audit_active=NULL;
    audit_inventory(a);
    audit_geometry(a);
    audit_scan(a);
    audit_ranges(a);
    a->views++;
    fflush(a->report);
}
