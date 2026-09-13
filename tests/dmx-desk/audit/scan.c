#include "api.h"
#include "desk_inert_mask.h"
#include <assert.h>
#include <stdlib.h>

void audit_scan(struct audit_context *a) {
    unsigned char *dead=calloc(AUDIT_PIXELS,1);
    int *queue=malloc(AUDIT_PIXELS*sizeof *queue);
    unsigned char *inert=malloc(AUDIT_PIXELS);
    assert(dead&&queue&&inert);
    desk_inert_mask(&a->model,&a->setup,&a->speed,inert);
    int conflicts=0,conflict_x=0,conflict_y=0;
    // An inert contract cannot mask any enabled control's painted envelope.
    for(int i=0;i<a->elements;i++)if(a->element[i].enabled) {
        struct desk_rect r=a->element[i].drawn;
        for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)
            if(x>=0&&x<DESK_W&&y>=0&&y<DESK_H&&inert[y*DESK_W+x]) {
                if(!conflicts)conflict_x=x,conflict_y=y;
                conflicts++;
            }
    }
    int unknown=0,unknown_x=0,unknown_y=0;
    for(int y=0;y<DESK_H;y++)for(int x=0;x<DESK_W;x++) {
        int value;
        int owner=audit_reach(a,x,y,&value);
        a->probes++;
        if(owner==-3) {if(!unknown)unknown_x=x,unknown_y=y;unknown++;}
        if(owner==-1&&!inert[y*DESK_W+x])dead[y*DESK_W+x]=1;
        if((owner>=0||owner==-2)&&inert[y*DESK_W+x]) {
            if(!conflicts)conflict_x=x,conflict_y=y;
            conflicts++;
        }
        if(owner<0)continue;
        struct audit_element *e=&a->element[owner];
        e->reachable++;
        if(x<e->min_x)e->min_x=x;
        if(x>e->max_x)e->max_x=x;
        if(y<e->min_y)e->min_y=y;
        if(y>e->max_y)e->max_y=y;
        if(!desk_rect_contains(e->allowance,x,y)) {
            if(!e->slop_count)e->slop_x=x,e->slop_y=y;
            e->slop_count++;
        }
    }
    if(conflicts)audit_note(a,0,"inert-contract","%d pixels of declared inert glass cover actions or enabled drawings; first=(%d,%d)",conflicts,conflict_x,conflict_y);
    if(unknown)audit_note(a,0,"inventory","%d pixels route to unregistered targets; first=(%d,%d)",unknown,unknown_x,unknown_y);
    for(int i=0;i<a->elements;i++) {
        struct audit_element *e=&a->element[i];
        if(!e->enabled)continue;
        if(!e->reachable)audit_note(a,0,"reachable","%s drawn=(%d,%d,%d,%d), zero reachable pixels",e->label,e->drawn.x,e->drawn.y,e->drawn.w,e->drawn.h);
        else if(e->reachable<AUDIT_MIN_AREA||e->max_x-e->min_x+1<AUDIT_MIN_WIDTH||e->max_y-e->min_y+1<AUDIT_MIN_HEIGHT)
            audit_note(a,1,"target-size","%s: %d px, reachable bounds=(%d,%d,%d,%d), minimum %dx%d / %d px (9 mm)",e->label,e->reachable,e->min_x,e->min_y,e->max_x-e->min_x+1,e->max_y-e->min_y+1,AUDIT_MIN_WIDTH,AUDIT_MIN_HEIGHT,AUDIT_MIN_AREA);
        if(e->slop_count)audit_note(a,0,"undeclared-hit","%s: %d pixels outside declared allowance=(%d,%d,%d,%d), drawn=(%d,%d,%d,%d), witness=(%d,%d)",e->label,e->slop_count,
            e->allowance.x,e->allowance.y,e->allowance.w,e->allowance.h,e->drawn.x,e->drawn.y,e->drawn.w,e->drawn.h,e->slop_x,e->slop_y);
    }
    // Four-neighbour components: diagonal contact alone does not merge bands.
    for(int start=0;start<AUDIT_PIXELS;start++)if(dead[start]) {
        int head=0,tail=1,edge=0,x0=DESK_W,y0=DESK_H,x1=-1,y1=-1;
        queue[0]=start;dead[start]=0;
        while(head<tail) {
            int p=queue[head++],x=p%DESK_W,y=p/DESK_W;
            if(x==0||y==0||x==DESK_W-1||y==DESK_H-1)edge=1;
            if(x<x0)x0=x;
            if(x>x1)x1=x;
            if(y<y0)y0=y;
            if(y>y1)y1=y;
            int neighbours[4]={x>0?p-1:-1,x+1<DESK_W?p+1:-1,y>0?p-DESK_W:-1,y+1<DESK_H?p+DESK_W:-1};
            for(int n=0;n<4;n++)if(neighbours[n]>=0&&dead[neighbours[n]]) {
                dead[neighbours[n]]=0;queue[tail++]=neighbours[n];
            }
        }
        if(edge||tail>AUDIT_MAX_DEAD_AREA)audit_note(a,0,"dead-band","%d inert pixels, edge=%s, bounds=(%d,%d,%d,%d), witness=(%d,%d)",tail,edge?"yes":"no",x0,y0,x1-x0+1,y1-y0+1,start%DESK_W,start/DESK_W);
    }
    // Exterior ring checks use the same effective routing, not TARGET_CONTENT.
    int exterior=0,wx=0,wy=0,who=-1;
    for(int y=-1;y<=DESK_H;y++)for(int x=-1;x<=DESK_W;x++) {
        if(x!=-1&&x!=DESK_W&&y!=-1&&y!=DESK_H)continue;
        int value,owner=audit_reach(a,x,y,&value);a->probes++;
        if(owner>=0||owner==-2) {if(!exterior)wx=x,wy=y,who=owner;exterior++;}
    }
    if(exterior)audit_note(a,0,"hit-on-screen","%d exterior pixels act; first %s at (%d,%d)",exterior,who==-2?"Setup outside-to-close":a->element[who].label,wx,wy);
    free(dead);free(queue);free(inert);
}
