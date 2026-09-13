#include "api.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "desk_master_track.h"

// Sensitivity checks mutate only an in-memory copy of the real show. Expected
// findings go to a separate stream, never into the real-show failure count.
int audit_selfcheck(struct audit_context *a,const struct vc_doc *doc) {
    struct audit_context *copy=malloc(sizeof *copy);assert(copy);
    *copy=*a;
    copy->report=tmpfile();assert(copy->report);
    copy->failures=0;copy->warnings=0;
    audit_backing(copy,doc);
    audit_ranges(copy);
    int ok=copy->failures==0,held=-1;
    for(int i=0;i<copy->elements;i++) {
        struct audit_element *e=&copy->element[i];
        if(e->kind!=A_CONTROL||!e->enabled)continue;
        struct desk_control *c=&copy->model.control[e->index];
        if(c->kind!=DESK_BURST&&c->kind!=DESK_HOLD)continue;
        held=e->index;
        int before=copy->failures,slot=c->hold_index;
        c->hold_index=-1;
        audit_backing(copy,doc);
        int value;
        int owner=audit_reach(copy,e->drawn.x+e->drawn.w/2,e->drawn.y+e->drawn.h/2,&value);
        ok=ok&&copy->failures>before&&owner!=i;
        c->hold_index=slot;
        break;
    }
    ok=ok&&held>=0;
    int moved=0;
    for(int i=0;i<copy->elements;i++) {
        struct audit_element *e=&copy->element[i];
        if(e->kind!=A_CONTROL||copy->model.control[e->index].kind!=DESK_MASTER)continue;
        struct desk_rect track=desk_master_track(desk_placement_of(&copy->model,e->index),copy->fonts.value);
        for(int j=0;j<copy->draws;j++)if(!copy->draw[j].text&&memcmp(&copy->draw[j].rect,&track,sizeof track)==0) {
            int before=copy->failures;
            copy->draw[j].rect.y-=50;
            audit_ranges(copy);
            ok=ok&&copy->failures>before;
            copy->draw[j].rect=track;moved=1;break;
        }
    }
    ok=ok&&moved;
    // Two independent candidate rectangles must expose overlap even when
    // first-match input dispatch would hide the second one's claim.
    if(copy->elements>=2) {
        copy->element[1].drawn=copy->element[0].drawn;
        copy->element[1].hit=copy->element[0].hit;
        audit_geometry(copy);
    }
    rewind(copy->report);
    char line[2048];int draw_overlap=0,hit_overlap=0;
    while(fgets(line,sizeof line,copy->report)) {
        if(strstr(line,"[draw-overlap]"))draw_overlap=1;
        if(strstr(line,"[hit-overlap]"))hit_overlap=1;
    }
    ok=ok&&draw_overlap&&hit_overlap;
    fclose(copy->report);free(copy);
    fprintf(a->report,"SELF-CHECK %s: missing hold backing/capture, offset drawn range, overlapping drawn/hit rectangles\n",ok?"PASS":"FAIL");
    if(!ok)audit_note(a,0,"detector-self-check","an injected defect escaped; do not trust coverage");
    return ok;
}
