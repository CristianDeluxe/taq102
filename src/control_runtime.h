#ifndef CONTROL_RUNTIME_H
#define CONTROL_RUNTIME_H
#include <stdint.h>
#include "action_worker.h"
#include "backlight.h"
#include "control_center.h"
#include "power_policy.h"
#include "settings.h"
#include "sleep_state.h"
#include "status.h"

/* Application preferences, telemetry and control side effects. No rendering. */
struct control_runtime {
    struct cc_model panel;
    struct settings settings;
    struct backlight backlight;
    struct power_policy *policy;
    struct action_worker *worker;
    struct sleep_timer idle;
    struct status status;
    const char *settings_path;
    int backlight_ready, wanted_wifi, wifi_exit, sleep_requested;
    int settings_pending, settings_failed;
    int64_t save_at_ms, read_at_ms, policy_at_ms;
    char action_note[48];
};
int control_runtime_init(struct control_runtime *r, const char *path, int64_t now);
void control_runtime_tick(struct control_runtime *r, int64_t now, int asleep);
void control_runtime_action(struct control_runtime *r, struct cc_result result, int64_t now);
void control_runtime_sleep(struct control_runtime *r, int64_t now);
void control_runtime_wake(struct control_runtime *r, int64_t now);
void control_runtime_free(struct control_runtime *r);
#endif
