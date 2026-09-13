#include "api.h"
#include <assert.h>
#include <stdlib.h>
// Render the real fitted glyphs into a local, unclipped canvas. This observes
// their actual ink, including ellipsis, instead of guessing from strlen.
void audit_text(struct font *f, struct canvas *c, int x, int y, int max_w, const char *s, uint32_t col) {
    int w = font_width(f,s);
    if (w > max_w) w = max_w;
    if (w > 0 && *s) {
        const int pad = 64;
        struct canvas scratch = {.w=w+2*pad, .h=font_height(f)+2*pad};
        scratch.px = calloc((size_t)scratch.w*scratch.h, sizeof *scratch.px);
        assert(scratch.px);
        font_draw_fit(f,&scratch,pad,pad+font_baseline(f),max_w,s,0xffffffff);
        int x0=scratch.w, y0=scratch.h, x1=-1, y1=-1;
        for (int yy=0; yy<scratch.h; yy++) for (int xx=0; xx<scratch.w; xx++)
            if (scratch.px[yy*scratch.w+xx]) {
                if(xx<x0)x0=xx; if(xx>x1)x1=xx;
                if(yy<y0)y0=yy; if(yy>y1)y1=yy;
            }
        if (x1>=x0) {
            int iw=x1-x0+1, ih=y1-y0+1;
            unsigned char *ink=calloc((size_t)iw*ih,1);
            assert(ink);
            for(int yy=0;yy<ih;yy++)for(int xx=0;xx<iw;xx++)
                ink[yy*iw+xx]=scratch.px[(yy+y0)*scratch.w+xx+x0]!=0;
            audit_record((struct desk_rect){x+x0-pad,y-font_baseline(f)+y0-pad,iw,ih},s,ink);
        }
        free(scratch.px);
    }
    font_draw_fit(f,c,x,y,max_w,s,col);
}
