#include "api.h"
#include "desk_setup_layout.h"
#include "desk_setup_row_hit.h"
#include "desk_setup_page_hit.h"

// Semantic adapter for the setup surface. Expressions reference its production
// layout constants. The independent painter trace must corroborate every box;
// capture probes must corroborate every hit. No allowance is inferred from hits.
void audit_setup_inventory(struct audit_context *a) {
    struct desk_setup *s=&a->setup;
    struct desk_rect r;
    if(s->kb.open) {
        struct kb_key keys[KB_MAX_KEYS];
        int n=keyboard_keys(&s->kb,keys,KB_MAX_KEYS);
        for(int i=0;i<n;i++) {
            r=(struct desk_rect){keys[i].x,keys[i].y,keys[i].w,keys[i].h};
            audit_add(a,A_KEY,i,0,keys[i].enabled,keys[i].label,r,r,r);
        }
        return;
    }
    if(s->confirm_open) {
        int buttons=s->confirm_known?3:2;
        int bw=(SETUP_CONFIRM_W-32-16*(buttons-1))/buttons;
        for(int i=0;i<buttons;i++) {
            int t=i==0?T_CONFIRM_NO:i==buttons-1?T_CONFIRM_YES:T_CONFIRM_NEW_KEY;
            r=(struct desk_rect){SETUP_CONFIRM_X+16+i*(bw+16),SETUP_CONFIRM_Y+SETUP_CONFIRM_H-64,bw,48};
            audit_add(a,A_SETUP,t,-1,1,i==0?"Cancel join":i==buttons-1?"Join":"New key",r,r,r);
        }
        return;
    }
    r=(struct desk_rect){SETUP_CLOSE_X,SETUP_CLOSE_Y,SETUP_CLOSE_W,SETUP_CLOSE_H};
    audit_add(a,A_SETUP,T_CLOSE,-1,1,"Close setup",r,r,r);
    for(int card=0;card<2;card++) {
        int x=card?SETUP_MASTER_X:SETUP_WIFI_X;
        int count=card?s->found_count:s->scan.count;
        int page=card?s->found_page:s->scan_page;
        if(count>SETUP_ROWS)for(int i=0;i<2;i++) {
            r=desk_setup_page_hit(x,i);
            struct desk_rect hit=r;
            audit_add(a,A_SETUP,card?(i?T_MASTER_NEXT:T_MASTER_PREV):(i?T_WIFI_NEXT:T_WIFI_PREV),-1,
                i?(page+1)*SETUP_ROWS<count:page>0,card?(i?"Master next":"Master previous"):(i?"Wi-Fi next":"Wi-Fi previous"),r,hit,hit);
        }
        for(int row=0;row<SETUP_ROWS && page*SETUP_ROWS+row<count;row++) {
            int index=page*SETUP_ROWS+row;
            r=(struct desk_rect){x+8,SETUP_ROWS_Y+row*SETUP_ROW_H+4,SETUP_CARD_W-16,SETUP_ROW_H-8};
            struct desk_rect hit=desk_setup_row_hit(x,row);
            audit_add(a,A_SETUP,card?T_MASTER_ROW:T_WIFI_ROW,index,
                card?1:!s->wifi_busy[0]&&wifi_scan_joinable(s->scan.network[index].security),
                card?s->found[index]:s->scan.network[index].ssid,r,hit,hit);
        }
    }
    r=(struct desk_rect){SETUP_WIFI_X+16,SETUP_BUTTONS_Y,SETUP_CARD_W-32,SETUP_BUTTON_H};
    audit_add(a,A_SETUP,T_SCAN,-1,s->wifi_available&&!s->wifi_busy[0],"Scan networks",r,r,r);
    int half=(SETUP_CARD_W-48)/2;
    r=(struct desk_rect){SETUP_MASTER_X+16,SETUP_BUTTONS_Y,half,SETUP_BUTTON_H};
    audit_add(a,A_SETUP,T_FIND,-1,1,"Find master",r,r,r);
    r.x+=half+16;
    audit_add(a,A_SETUP,T_TYPE,-1,1,"Type IP",r,r,r);
    r=(struct desk_rect){SETUP_FADER_X,SETUP_FADER_Y,SETUP_FADER_W,SETUP_FADER_H};
    audit_add(a,A_SETUP,T_FADER,-1,1,"Brightness",r,r,r);
    r=(struct desk_rect){SETUP_TOGGLE_X,SETUP_TOGGLE_Y,SETUP_TOGGLE_W,SETUP_TOGGLE_H};
    audit_add(a,A_SETUP,T_TOGGLE,-1,1,"Power-aware dimming",r,r,r);
}
