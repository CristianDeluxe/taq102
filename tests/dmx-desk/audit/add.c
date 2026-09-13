#include "api.h"
#include <stdlib.h>
void audit_add(struct audit_context *a,int kind,int index,int sub,int enabled,const char *label,
               struct desk_rect drawn,struct desk_rect hit,struct desk_rect allowance) {
    if(a->elements==AUDIT_MAX_ELEMENTS) { fprintf(stderr,"audit element capacity exhausted\n"); exit(2); }
    struct audit_element *e=&a->element[a->elements++];
    *e=(struct audit_element){.kind=kind,.index=index,.sub=sub,.enabled=enabled,
        .drawn=drawn,.hit=hit,.allowance=allowance,.min_x=DESK_W,.min_y=DESK_H,.max_x=-1,.max_y=-1};
    snprintf(e->label,sizeof e->label,"%s",label);
}
