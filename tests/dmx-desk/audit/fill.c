#include "api.h"
void audit_fill(struct canvas *c, int x, int y, int w, int h, uint32_t col) {
    audit_record((struct desk_rect){x,y,w,h}, NULL, NULL);
    canvas_fill_rect(c,x,y,w,h,col);
}
