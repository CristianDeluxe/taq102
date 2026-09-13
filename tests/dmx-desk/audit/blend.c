#include "api.h"
#include "canvas_blend.h"
// Icons blend alpha pixels directly. Observe rejected pixels before the real
// clip discards them; do not allocate a draw record for every valid icon pixel.
void audit_blend(struct canvas *c,int x,int y,uint32_t col) {
    if((col>>24)&&(x<0||y<0||x>=DESK_W||y>=DESK_H))
        audit_record((struct desk_rect){x,y,1,1},NULL,NULL);
    canvas_blend(c,x,y,col);
}
