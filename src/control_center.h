#ifndef CONTROL_CENTER_H
#define CONTROL_CENTER_H
#include <stdint.h>
#include "touch_input.h"
#include "wifi_state.h"

enum cc_action { CC_NONE, CC_SET_BRIGHTNESS, CC_SET_AUTO, CC_SLEEP_NOW,
    CC_SET_SLEEP_MINUTES, CC_WIFI_TOGGLE, CC_REBOOT_RESCUE,
    CC_REBOOT_LOADER, CC_SAVE_SETTINGS };
struct cc_model {
    int open; float slide; int64_t slide_start_ms;
    int brightness, brightness_max, auto_on, sleep_minutes;
    enum wifi_state wifi; char ssid[33]; char addr[64]; int level_dbm;
    int cap, plugged; char batt_word[16]; char build[64]; char kernel[32]; char note[48];
    int armed; int64_t armed_at_ms;
    int dragging_brightness; int64_t last_drag_paint_ms; int dirty;
    int capture_slot, capture_tile; float capture_y; int64_t capture_at_ms;
    int pending_brightness; float slide_from;
};
struct cc_result { enum cc_action action; int arg; };
void cc_init(struct cc_model *m, int brightness, int brightness_max, int auto_on, int sleep_minutes);
void cc_open(struct cc_model *m, int64_t now_ms);
void cc_close(struct cc_model *m, int64_t now_ms);
int cc_visible(const struct cc_model *m);
/*
 * Insufficient capacity returns -required without changing the model or output.
 * Retry the same event/time with that capacity. At most three actions are needed.
 * now_ms is nonnegative monotonic milliseconds; touch_event.t is not used.
 */
int cc_handle(struct cc_model *m, const struct touch_event *ev, int64_t now_ms,
              struct cc_result *out, int max);
void cc_tick(struct cc_model *m, int64_t now_ms);
float cc_slide_offset(const struct cc_model *m);
#endif
