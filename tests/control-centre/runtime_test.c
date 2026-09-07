// SOURCES: control_runtime.c control_input.c control_center.c settings.c settings_store.c power_policy.c sleep_state.c touch_input.c touch_router.c touch_flip.c oneeuro.c wifi_status.c
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "control_runtime.h"
#include "control_input.h"
#include "control_center_layout.h"
#include "reboot_target.h"

/* Link-time hardware boundaries: no real commands, sysfs or reboot targets. */
static int level = 255, writes, busy, completion, worker_exit, saves;
static int64_t clock_ms;
static struct status telemetry;
struct action_worker { int unused; };
static struct action_worker worker;
int backlight_open(struct backlight *b) { b->max = 255; return 0; }
int backlight_get(const struct backlight *b) { (void)b; return level; }
int backlight_set(const struct backlight *b, int value) { (void)b; level = value; writes++; return 0; }
void status_read(struct status *s) { *s = telemetry; s->read_ms = clock_ms; }
struct action_worker *aw_new(void) { return &worker; }
void aw_free(struct action_worker *w) { assert(w == &worker); }
int aw_busy(const struct action_worker *w) { (void)w; return busy; }
int aw_poll(struct action_worker *w, int *result) {
    (void)w;
    if (!completion) return 0;
    completion = 0; busy = 0; *result = worker_exit; return 1;
}
int aw_start(struct action_worker *w, const char *const argv[], int timeout) {
    (void)w;
    assert(strcmp(argv[0], "/usr/sbin/taq102-wifi") == 0 && timeout == 45);
    assert(!argv[2]);
    if (busy) return -1;
    busy = 1; return 0;
}
int reboot_target_rescue(char *err, size_t n) { snprintf(err, n, "Fake rescue failure"); saves++; return -1; }
void reboot_target_loader(void) { saves++; errno = ENOENT; }
static void tick(struct control_runtime *r, int64_t now, int asleep) {
    clock_ms = now; control_runtime_tick(r, now, asleep);
}
static void action(struct control_runtime *r, enum cc_action a, int arg, int64_t now) {
    control_runtime_action(r, (struct cc_result){a, arg}, now);
}
static void event(struct control_input *i, struct control_runtime *r, enum touch_kind k, float x, float y, double t) {
    struct touch_event e = {k, 0, x, y, t}; control_input_feed(i, r, &e, 1);
}
int main(void) {
    char dir[512]; snprintf(dir, sizeof dir, "%s/runtime-XXXXXX", getenv("TEST_OUT"));
    assert(mkdtemp(dir));
    char path[600]; snprintf(path, sizeof path, "%s/settings", dir);
    telemetry = (struct status){.usb_valid=1, .ac_valid=1, .online_valid=1,
        .current_valid=1, .cap_valid=1, .word_valid=1, .cap=80, .current_ua=-100000};
    strcpy(telemetry.word, "Discharging");
    struct control_runtime r;
    assert(control_runtime_init(&r, path, 0) == 0);
    for (int t=0; t<=30000; t+=1000) tick(&r, t, 0);
    assert(level == 128 && r.panel.brightness == 128 && r.settings.brightness == 200);
    action(&r, CC_SET_AUTO, 0, 31000); assert(level == 200);
    action(&r, CC_SET_BRIGHTNESS, 96, 31100); assert(level == 96);
    assert(access(path, F_OK) < 0);
    tick(&r, 33099, 0); assert(access(path, F_OK) < 0);
    tick(&r, 33100, 0); assert(access(path, F_OK) == 0);
    struct settings saved; assert(settings_load(&saved, path) == 0 && saved.brightness == 96 && !saved.brightness_auto);
    action(&r, CC_SET_AUTO, 1, 34000); assert(level == 128);
    int before = writes;
    control_runtime_sleep(&r, 35000);
    for (int t=40000; t<100000; t+=1000) tick(&r, t, 1);
    assert(writes == before && !r.sleep_requested);
    control_runtime_wake(&r, 100000); assert(r.idle.deadline_ms == 400000);
    action(&r, CC_SET_SLEEP_MINUTES, 1, 100000);
    telemetry.plugged = telemetry.usb_online = 1;
    tick(&r, 170000, 0); assert(!r.sleep_requested);
    telemetry.plugged = telemetry.usb_online = 0;
    tick(&r, 171000, 0); assert(r.idle.deadline_ms == 231000 && !r.sleep_requested);
    tick(&r, 231000, 0); assert(r.sleep_requested);
    control_runtime_sleep(&r, 231000); control_runtime_wake(&r, 232000);

    struct touch_flip flip; assert(touch_flip_configure(&flip, 1024, 600, "xy") == 0);
    struct control_input input; assert(control_input_init(&input, &flip) == 0);
    /* One render batch opens the panel, then taps Auto with a new contact. */
    struct touch_event batch[] = {{TOUCH_DOWN,0,900,10,233}, {TOUCH_MOVE,0,900,80,233.1},
        {TOUCH_UP,0,900,80,233.2}, {TOUCH_DOWN,0,880,110,233.3}, {TOUCH_UP,0,880,110,233.4}};
    control_input_feed(&input, &r, batch, 5);
    assert(cc_visible(&r.panel) && !r.settings.brightness_auto && !input.cube.slot[0].active);
    assert(level == 96);
    /* Sliding alone does not dirty the texture. */
    r.panel.dirty = 0; cc_tick(&r.panel, 233500); assert(!r.panel.dirty);
    event(&input, &r, TOUCH_DOWN, CC_BRIGHT.x+50, 250, 234);
    event(&input, &r, TOUCH_UP, CC_BRIGHT.x+50, 180, 234.1);
    assert(settings_load(&saved, path) == 0 && saved.brightness == level);
    assert(r.settings.brightness == level && !saved.brightness_auto);
    action(&r, CC_SET_AUTO, 1, 234200);
    assert(level == 128);
    float same_level_y = CC_BRIGHT.y + CC_BRIGHT.h - 1.f - 120.f * (CC_BRIGHT.h - 1.f) / 247.f;
    event(&input, &r, TOUCH_DOWN, CC_BRIGHT.x+50, same_level_y, 234.3);
    event(&input, &r, TOUCH_UP, CC_BRIGHT.x+50, same_level_y, 234.4);
    assert(level == 128 && r.settings.brightness == 128 && !r.settings.brightness_auto);
    event(&input, &r, TOUCH_DOWN, CC_RESCUE.x+30, CC_RESCUE.y+30, 235);
    event(&input, &r, TOUCH_CANCEL, CC_RESCUE.x+30, CC_RESCUE.y+30, 235.6);
    assert(!r.panel.armed && !saves);
    event(&input, &r, TOUCH_DOWN, CC_RESCUE.x+30, CC_RESCUE.y+30, 236);
    event(&input, &r, TOUCH_UP, CC_RESCUE.x+30, CC_RESCUE.y+30, 236.1);
    assert(r.panel.armed);
    control_input_reset(&input, &r, 1); assert(!r.panel.armed && r.panel.capture_slot < 0);
    event(&input, &r, TOUCH_DOWN, 100, 500, 237);
    event(&input, &r, TOUCH_UP, 100, 500, 237.1);
    assert(!r.panel.open && cc_visible(&r.panel));
    event(&input, &r, TOUCH_DOWN, 100, 500, 237.15);
    assert(!input.cube.slot[0].active); /* closing slide still owns input */
    event(&input, &r, TOUCH_UP, 100, 500, 237.2);
    event(&input, &r, TOUCH_DOWN, 100, 500, 238);
    assert(input.cube.slot[0].active);
    assert(r.idle.deadline_ms == 298000);
    control_input_reset(&input, &r, 0); assert(!input.cube.slot[0].active);
    /* Wi-Fi that init raises after the app started must stop reading as off. */
    assert(!r.wanted_wifi && r.panel.wifi == WIFI_OFF);
    telemetry.have_wifi = 1; telemetry.level = -47;
    strcpy(telemetry.addr, "192.168.1.40");
    tick(&r, 238500, 0); assert(r.wanted_wifi && r.panel.wifi == WIFI_CONNECTED);
    action(&r, CC_WIFI_TOGGLE, 1, 239000); assert(busy && r.panel.wifi == WIFI_STARTING);
    action(&r, CC_WIFI_TOGGLE, 0, 239100); assert(r.wanted_wifi == 1);
    completion=1; worker_exit=-1; tick(&r, 240000, 0); assert(r.panel.wifi == WIFI_FAILED);
    r.settings_path = "/nonexistent/taq102/settings";
    action(&r, CC_SAVE_SETTINGS, 0, 240000);
    assert(r.settings_failed && strcmp(r.panel.note, "Settings not saved") == 0);
    assert(!saves); /* No reboot wrapper was reached by ordinary input. */
    control_input_free(&input); control_runtime_free(&r);
    unlink(path); rmdir(dir);
    puts("runtime ok");
    return 0;
}
