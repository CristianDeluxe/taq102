// SOURCES: ../tests/dmx-desk/audit/pager_selfcheck.c desk_inert_mask.c desk_region_mark.c desk_setup_inert_mask.c desk_speed_inert_mask.c desk_model.c desk_master_level_at.c desk_master_track.c desk_pager_caption.c desk_pager_bank_caption.c ../tests/dmx-desk/audit/icon.c ../tests/dmx-desk/audit/blend.c desk_caption.c desk_view.c desk_view_pager.c desk_pager_label.c desk_layout_resolve.c desk_show_layout.c showmap.c showmap_validate.c vcjson.c canvas.c canvas_blend.c font.c desk_fonts.c desk_input.c desk_pager_hit.c desk_build_holds.c desk_hold.c desk_speed.c desk_tap.c speed_factor.c desk_setup.c desk_brightness_track.c desk_brightness_level_at.c desk_setup_row_hit.c desk_setup_page_hit.c keyboard.c wifi_scan.c desk_conf.c ../tests/dmx-desk/audit/active.c ../tests/dmx-desk/audit/note.c ../tests/dmx-desk/audit/record.c ../tests/dmx-desk/audit/round.c ../tests/dmx-desk/audit/fill.c ../tests/dmx-desk/audit/text.c ../tests/dmx-desk/audit/glyph_text.c ../tests/dmx-desk/audit/paint.c ../tests/dmx-desk/audit/speed_paint.c ../tests/dmx-desk/audit/setup_paint.c ../tests/dmx-desk/audit/keyboard_paint.c ../tests/dmx-desk/audit/add.c ../tests/dmx-desk/audit/inventory.c ../tests/dmx-desk/audit/setup_inventory.c ../tests/dmx-desk/audit/reach.c ../tests/dmx-desk/audit/geometry.c ../tests/dmx-desk/audit/scan.c ../tests/dmx-desk/audit/view.c ../tests/dmx-desk/audit/backing.c ../tests/dmx-desk/audit/ranges.c ../tests/dmx-desk/audit/selfcheck.c
// OPTIMIZE: 2
// A full-pixel audit of the real Vibra show. Unresolved findings fail the gate.
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "audit/api.h"
#include "desk_build_holds.h"
#include "showmap_validate.h"
#include "touch_input.h"

int main(void) {
    // Bound CPU cost without sampling or skipping views. A stalled/slow audit
    // fails the gate; the flushed report identifies its last completed view.
    alarm(120);
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC,&started);
    struct audit_context *a=calloc(1,sizeof *a);
    assert(a);
    size_t len;
    FILE *f=fopen("tests/dmx-desk/fixtures/vc-vibra.json","rb");
    assert(f);
    assert(fseek(f,0,SEEK_END)==0);
    long size=ftell(f);assert(size>0);
    rewind(f);len=(size_t)size;
    char *json=malloc(len+1);assert(json);
    assert(fread(json,1,len,f)==len);fclose(f);json[len]='\0';
    struct vc_doc console;
    assert(vc_parse(json,len,&console)==0);free(json);
    struct show_map map;
    assert(showmap_load("show/vibra.desk.json",&map)==0);
    assert(showmap_build(&a->model,&map,&console)>100);
    struct desk_layout layout;
    assert(desk_layout_resolve(&map,DESK_MASTER_INDEX(&map),DESK_PANIC_INDEX(&map),DESK_HAZE_OFF_INDEX(&map),DESK_TEMPO_INDEX(&map),&layout)==0);
    desk_set_layout(&a->model,&layout);
    int owners[TOUCH_MAX_SLOTS];
    desk_build_holds(&a->hold,&a->model,owners);
    desk_hold_set_link(&a->hold,1);
    desk_speed_init(&a->speed,&map);
    desk_speed_validate(&a->speed,&console);
    desk_speed_set_link(&a->speed,1,0);
    desk_set_link(&a->model,DESK_LINK_READY);
    desk_setup_init(&a->setup);
    assert(desk_fonts_open(&a->fonts,"br2-external/package/taq102-fonts/fonts")==0);
    a->canvas=(struct canvas){.px=calloc(AUDIT_PIXELS,4),.w=DESK_W,.h=DESK_H};assert(a->canvas.px);
    const char *out=getenv("TEST_OUT");
    char path[1024];snprintf(path,sizeof path,"%s/desk-geometry-report.txt",out?out:"/tmp");
    a->report=fopen(path,"w");assert(a->report);
    fprintf(a->report,"GEOMETRY AUDIT: real show/vibra.desk.json + fixtures/vc-vibra.json\n"
        "Full 1024x600 pixel sweep per view, exterior ring, analytical rectangle intersections.\n"
        "9 mm target: %dx%d / %d px; 4-connected undeclared dead component >%d px OR any screen edge fails.\n"
        "Rectangles are (x,y,width,height), half-open. Range tolerance=%d output unit.\n",
        AUDIT_MIN_WIDTH,AUDIT_MIN_HEIGHT,AUDIT_MIN_AREA,AUDIT_MAX_DEAD_AREA,AUDIT_RANGE_TOLERANCE);
    int enabled=0;
    for(int i=0;i<a->model.count;i++)enabled+=a->model.control[i].enabled!=0;
    fprintf(a->report,"COVERAGE pages=%d controls=%d enabled=%d hold-slots=%d speed-dials=%d placements=%d\n",
        layout.pages,a->model.count,enabled,a->hold.count,a->speed.dials,layout.placements);
    audit_backing(a,&console);
    // The widest real room-state word stresses chrome on every page and bank.
    int widest=-1,width=-1;
    for(int i=0;i<a->model.count;i++)if(a->model.control[i].kind==DESK_CUE&&a->model.control[i].role==MAP_ROLE_STATE) {
        int w=font_width(a->fonts.small,a->model.control[i].label);
        if(w>width)widest=i,width=w;
    }
    if(widest>=0)a->model.control[widest].state=DESK_ON;
    int pager_checked=0;
    for(int page=0;page<layout.pages;page++)for(int bank=0;bank<layout.banks[page];bank++) {
        desk_set_view(&a->model,page,bank);
        audit_view(a,widest>=0?a->model.control[widest].label:"ready");
        if(page==0&&bank==0)audit_selfcheck(a,&console);
        if(!pager_checked&&layout.banks[page]>1) {
            audit_pager_selfcheck(a);pager_checked=1;
        }
    }
    // Every other real room-state label gets its own render and sweep too.
    desk_set_view(&a->model,0,0);
    if(widest>=0)a->model.control[widest].state=DESK_OFF;
    audit_view(a,"no room running");
    for(int i=0;i<a->model.count;i++)if(i!=widest&&a->model.control[i].kind==DESK_CUE&&a->model.control[i].role==MAP_ROLE_STATE) {
        a->model.control[i].state=DESK_ON;
        audit_view(a,a->model.control[i].label);
        a->model.control[i].state=DESK_OFF;
    }
    // Future captions must respect the same reserved status run.
    if(widest>=0) {
        char saved[sizeof a->model.control[widest].label];
        memcpy(saved,a->model.control[widest].label,sizeof saved);
        memset(a->model.control[widest].label,'W',sizeof saved-1);
        a->model.control[widest].label[sizeof saved-1]='\0';
        a->model.control[widest].state=DESK_ON;
        audit_view(a,"maximum-length room caption");
        memcpy(a->model.control[widest].label,saved,sizeof saved);
        a->model.control[widest].state=DESK_OFF;
    }
    // Setup's list data comes from I/O, not the show. Deterministic non-secret
    // inputs cover empty/full/last list pages, both confirmations and keyboards.
    desk_setup_open(&a->setup);a->model.setup_open=1;
    a->setup.wifi_available=1;
    audit_view(a,"setup empty");
    a->setup.scan.count=4;a->setup.found_count=4;
    for(int i=0;i<4;i++) {
        snprintf(a->setup.scan.network[i].ssid,sizeof a->setup.scan.network[i].ssid,"Audit network %d",i+1);
        a->setup.scan.network[i].security=WIFI_OPEN;
        snprintf(a->setup.found[i],sizeof a->setup.found[i],"192.0.2.%d",i+1);
    }
    audit_view(a,"setup lists first page");
    a->setup.scan_page=1;a->setup.found_page=1;
    audit_view(a,"setup lists last page");
    a->setup.confirm_open=1;
    snprintf(a->setup.pending_ssid,sizeof a->setup.pending_ssid,"Audit network");
    audit_view(a,"setup join confirmation");
    a->setup.confirm_known=1;
    audit_view(a,"setup known-network confirmation");
    a->setup.confirm_open=0;
    keyboard_open(&a->setup.kb,KB_NUMERIC,"Audit address","192.0.2.1",0,7,24);
    audit_view(a,"setup numeric keyboard");
    keyboard_open(&a->setup.kb,KB_TEXT,"Audit text","",1,0,63);
    for(int layer=KB_LOWER;layer<=KB_SYMBOLS_2;layer++) {
        char state[64];snprintf(state,sizeof state,"setup text keyboard layer %d",layer);
        a->setup.kb.layer=layer;audit_view(a,state);
    }
    struct timespec finished;
    clock_gettime(CLOCK_MONOTONIC,&finished);
    fprintf(a->report,"DURATION %.3f seconds (hard deadline 120 seconds)\n",
        (double)(finished.tv_sec-started.tv_sec)+(finished.tv_nsec-started.tv_nsec)/1e9);
    fprintf(a->report,"SUMMARY views=%d probes=%ld failures=%d warnings=%d\n",a->views,a->probes,a->failures,a->warnings);
    fclose(a->report);
    // Full actionable output goes to stdout as well as the durable test artifact.
    f=fopen(path,"r");assert(f);
    char line[2048];while(fgets(line,sizeof line,f))fputs(line,stdout);fclose(f);
    printf("Report: %s\n",path);
    int failed=a->failures>0;
    for(int i=0;i<a->draws;i++)free(a->draw[i].ink);
    free(a->canvas.px);desk_fonts_close(&a->fonts);vc_free(&console);free(a);
    return failed?1:0;
}
