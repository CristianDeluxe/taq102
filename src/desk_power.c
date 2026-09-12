#include "desk_power.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "settings_store.h"

#define POLICY_MS 10000
#define SAVE_DELAY_MS 2000

static void apply(struct desk_power *p, int level) {
    if (level < 8)
        level = 8;
    if (level > p->max)
        level = p->max;
    if (p->backlight_ready && backlight_set(&p->backlight, level) < 0)
        fprintf(stderr, "backlight: cannot apply %d\n", level);
    p->level = level;
}

static void save(struct desk_power *p) {
    p->save_pending = 0;
    p->save_failed = !settings_store_available(p->settings_path) ||
                     settings_save(&p->settings, p->settings_path) < 0;
    if (p->save_failed)
        fprintf(stderr, "settings: cannot save %s\n", p->settings_path);
}

int desk_power_init(struct desk_power *p, const char *settings_path,
                    const char *backlight_root, int64_t now_ms) {
    memset(p, 0, sizeof *p);
    p->settings_path = settings_path;
    if (settings_load(&p->settings, settings_path) < 0 && errno != ENOENT)
        p->save_failed = 1;
    p->backlight_ready = backlight_open_at(&p->backlight, backlight_root) == 0;
    p->max = p->backlight_ready ? p->backlight.max : 255;
    p->policy = power_policy_new(p->max, now_ms);
    if (!p->policy)
        return -1;
    int initial = p->backlight_ready ? backlight_get(&p->backlight) : -1;
    apply(p, p->settings.brightness_auto && initial >= 8 ? initial : p->settings.brightness);
    p->policy_at_ms = now_ms;
    return 0;
}

void desk_power_set_level(struct desk_power *p, int level, int64_t now_ms) {
    p->settings.brightness = level;
    apply(p, level);
    p->save_pending = 1;
    p->save_at_ms = now_ms + SAVE_DELAY_MS;
}

void desk_power_set_aware(struct desk_power *p, int on, int64_t now_ms) {
    p->settings.brightness_auto = on ? 1 : 0;
    // Samples taken at a hand-set level say nothing about the policy's own.
    struct status none;
    memset(&none, 0, sizeof none);
    power_policy_step(p->policy, &none, now_ms, 1);
    if (!on)
        apply(p, p->settings.brightness);
    p->policy_at_ms = now_ms;
    p->save_pending = 1;
    p->save_at_ms = now_ms + SAVE_DELAY_MS;
}

void desk_power_tick(struct desk_power *p, const struct status *status, int64_t now_ms) {
    if (p->settings.brightness_auto && now_ms >= p->policy_at_ms) {
        int64_t at = status->read_ms > now_ms ? status->read_ms : now_ms;
        int level = power_policy_step(p->policy, status, at, 0);
        if (level >= 8)
            apply(p, level);
        p->policy_at_ms = now_ms + POLICY_MS;
    }
    if (p->save_pending && now_ms >= p->save_at_ms)
        save(p);
}

int desk_power_aware(const struct desk_power *p) {
    return p->settings.brightness_auto;
}

void desk_power_free(struct desk_power *p) {
    if (p->save_pending)
        save(p);
    power_policy_free(p->policy);
    p->policy = NULL;
}
