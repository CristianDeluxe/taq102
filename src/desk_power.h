// The screen's brightness for the desk: the level the operator set, kept in
// the same settings file glcube keeps (`/data/taq102.conf`), and the
// battery-aware policy that lowers it when the charger is gone, when the
// operator wants that. Saves are deferred two seconds so a drag is one write.
#ifndef DESK_POWER_H
#define DESK_POWER_H

#include <stdint.h>

#include "backlight.h"
#include "power_policy.h"
#include "settings.h"
#include "status.h"

struct desk_power {
    struct settings settings;
    const char *settings_path;
    struct backlight backlight;
    int backlight_ready;
    int max;
    int level;                  // what the panel is at now
    struct power_policy *policy;
    int64_t policy_at_ms, save_at_ms;
    int save_pending, save_failed;
};

// Opens the backlight under `backlight_root` (`/sys/class/backlight`) and
// loads the settings. Applies the saved level, or under the policy the
// level the panel already has. Works without a backlight: levels are kept,
// nothing is written.
int desk_power_init(struct desk_power *p, const char *settings_path,
                    const char *backlight_root, int64_t now_ms);
void desk_power_set_level(struct desk_power *p, int level, int64_t now_ms);
void desk_power_set_aware(struct desk_power *p, int on, int64_t now_ms);
// Runs the policy every ten seconds and the deferred save.
void desk_power_tick(struct desk_power *p, const struct status *status, int64_t now_ms);
int desk_power_aware(const struct desk_power *p);
void desk_power_free(struct desk_power *p);

#endif
