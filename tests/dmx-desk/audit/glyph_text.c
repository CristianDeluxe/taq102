#include "api.h"
void audit_glyph_text(struct canvas *c, int x, int y, const char *s, int scale, uint32_t col) {
    audit_record((struct desk_rect){x,y,canvas_text_width(s,scale),5*scale},s,NULL);
    canvas_text(c,x,y,s,scale,col);
}
