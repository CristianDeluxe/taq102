#include "api.h"
#include <string.h>

static int contains(struct desk_rect a,struct desk_rect b) {
    return b.x>=a.x && b.y>=a.y && b.x+b.w<=a.x+a.w && b.y+b.h<=a.y+a.h;
}
static int intersects(struct desk_rect a,struct desk_rect b) {
    return a.w>0&&a.h>0&&b.w>0&&b.h>0&&a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;
}
static int ink_at(const struct audit_draw *d,int x,int y) {
    return desk_rect_contains(d->rect,x,y) && (!d->ink || d->ink[(y-d->rect.y)*d->rect.w+x-d->rect.x]);
}
void audit_geometry(struct audit_context *a) {
    struct desk_rect screen={0,0,DESK_W,DESK_H};
    for(int i=0;i<a->elements;i++) {
        struct audit_element *e=&a->element[i];
        if(!contains(screen,e->drawn)||!contains(screen,e->hit))
            audit_note(a,0,"on-screen","%s drawn=(%d,%d,%d,%d) hit=(%d,%d,%d,%d)",e->label,
                e->drawn.x,e->drawn.y,e->drawn.w,e->drawn.h,e->hit.x,e->hit.y,e->hit.w,e->hit.h);
        for(int j=0;j<a->draws;j++) {
            struct audit_draw *d=&a->draw[j];
            if(!d->text && memcmp(&d->rect,&e->drawn,sizeof d->rect)==0)e->painted=1;
            // Tabs/icons/arrow glyphs have semantic target boxes without a
            // permanent fill. Require visible ink inside those production boxes.
            if((e->kind==A_TAB||e->kind==A_GEAR||e->kind==A_LOCK||
                (e->kind==A_SETUP&&(e->index==T_WIFI_PREV||e->index==T_WIFI_NEXT||e->index==T_MASTER_PREV||e->index==T_MASTER_NEXT))) &&
                d->text && contains(e->drawn,d->rect))e->painted=1;
        }
        if(e->kind==A_GEAR||e->kind==A_LOCK) {
            for(int y=e->drawn.y;y<e->drawn.y+e->drawn.h;y++)for(int x=e->drawn.x;x<e->drawn.x+e->drawn.w;x++)
                if(x>=0&&x<DESK_W&&y>=0&&y<DESK_H&&a->canvas.px[y*DESK_W+x]!=DESK_GLASS)e->painted=1;
        }
        if(e->enabled&&!e->painted)
            audit_note(a,0,"draw-backing","%s: no matching real paint for drawn=(%d,%d,%d,%d)",e->label,e->drawn.x,e->drawn.y,e->drawn.w,e->drawn.h);
        for(int j=0;j<i;j++) {
            struct audit_element *other=&a->element[j];
            if(intersects(e->drawn,other->drawn))
                audit_note(a,0,"draw-overlap","%s (%d,%d,%d,%d) / %s (%d,%d,%d,%d); pixel=(%d,%d)",
                    e->label,e->drawn.x,e->drawn.y,e->drawn.w,e->drawn.h,other->label,other->drawn.x,other->drawn.y,other->drawn.w,other->drawn.h,
                    e->drawn.x>other->drawn.x?e->drawn.x:other->drawn.x,e->drawn.y>other->drawn.y?e->drawn.y:other->drawn.y);
            if(e->enabled&&other->enabled&&intersects(e->hit,other->hit))
                audit_note(a,0,"hit-overlap","%s hit=(%d,%d,%d,%d) / %s hit=(%d,%d,%d,%d); pixel=(%d,%d)",
                    e->label,e->hit.x,e->hit.y,e->hit.w,e->hit.h,other->label,other->hit.x,other->hit.y,other->hit.w,other->hit.h,
                    e->hit.x>other->hit.x?e->hit.x:other->hit.x,e->hit.y>other->hit.y?e->hit.y:other->hit.y);
        }
    }
    for(int i=0;i<a->draws;i++) {
        const struct audit_draw *d=&a->draw[i];
        if(!contains(screen,d->rect))audit_note(a,0,"paint-on-screen","%s rect=(%d,%d,%d,%d)",d->label,d->rect.x,d->rect.y,d->rect.w,d->rect.h);
        if(!d->text)continue;
        for(int j=0;j<a->elements;j++) {
            const struct audit_element *e=&a->element[j];
            if(!intersects(d->rect,e->drawn)||contains(e->drawn,d->rect))continue;
            int px=-1,py=-1;
            for(int y=d->rect.y;y<d->rect.y+d->rect.h&&px<0;y++)for(int x=d->rect.x;x<d->rect.x+d->rect.w;x++)
                if(desk_rect_contains(e->drawn,x,y)&&ink_at(d,x,y)){px=x;py=y;break;}
            if(px>=0)audit_note(a,0,"text-over-control","text \"%s\" (%d,%d,%d,%d) crosses %s (%d,%d,%d,%d); ink pixel=(%d,%d)",
                d->label,d->rect.x,d->rect.y,d->rect.w,d->rect.h,e->label,e->drawn.x,e->drawn.y,e->drawn.w,e->drawn.h,px,py);
        }
        for(int j=0;j<i;j++) {
            const struct audit_draw *b=&a->draw[j];
            if(!b->text||!intersects(d->rect,b->rect))continue;
            int px=-1,py=-1;
            for(int y=d->rect.y;y<d->rect.y+d->rect.h&&px<0;y++)for(int x=d->rect.x;x<d->rect.x+d->rect.w;x++)
                if(ink_at(d,x,y)&&ink_at(b,x,y)){px=x;py=y;break;}
            if(px>=0)audit_note(a,0,"text-overlap","\"%s\" (%d,%d,%d,%d) / \"%s\" (%d,%d,%d,%d); ink pixel=(%d,%d)",
                d->label,d->rect.x,d->rect.y,d->rect.w,d->rect.h,b->label,b->rect.x,b->rect.y,b->rect.w,b->rect.h,px,py);
        }
    }
}
