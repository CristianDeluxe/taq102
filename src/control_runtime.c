#include "control_runtime.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include "reboot_target.h"
#include "settings_store.h"
#include "wifi_status.h"

static void apply(struct control_runtime *r, int level) {
    if (level < 8) return;
    if (level > r->panel.brightness_max) level = r->panel.brightness_max;
    if (!r->backlight_ready || backlight_set(&r->backlight, level) < 0) {
        snprintf(r->action_note, sizeof r->action_note, "Backlight unavailable");
        fprintf(stderr, "backlight: cannot apply %d\n", level);
    }
    if (r->panel.brightness != level) r->panel.dirty = 1;
    r->panel.brightness = level;
}

static void save(struct control_runtime *r) {
    r->settings_pending = 0;
    r->settings_failed = !settings_store_available(r->settings_path) ||
        settings_save(&r->settings, r->settings_path) < 0;
    if (r->settings_failed) fprintf(stderr, "settings: cannot save %s\n", r->settings_path);
    r->panel.dirty = 1;
}

static void read_status(struct control_runtime *r, int64_t now) {
    struct status next;
    status_read(&next);
    if (next.online_valid && !next.plugged &&
        (!r->status.online_valid || r->status.plugged)) sleep_timer_touch(&r->idle, now);
    r->status = next;
    /* `taq102-wifi up` and the app are both `::once` entries in inittab, so at
     * init the link is still seconds away: the module load alone waits up to
     * eight, then association and DHCP. Latching the wanted state from that
     * first sample left the panel offering "Turn on" for a Wi-Fi that came up
     * behind it. Adopt an observed link instead, except while an action of our
     * own is in flight -- a toggle to off owns the state until its worker
     * exits, by which time `down` has removed the module. */
    if (r->status.have_wifi && !aw_busy(r->worker)) r->wanted_wifi = 1;
    r->read_at_ms = now + 1000;
}

static void readouts(struct control_runtime *r) {
    struct cc_model *m = &r->panel;
    struct cc_model before = *m;
    m->cap = r->status.cap;
    m->plugged = r->status.plugged;
    m->level_dbm = r->status.level;
    snprintf(m->batt_word, sizeof m->batt_word, "%s", r->status.word);
    snprintf(m->addr, sizeof m->addr, "%s", r->status.addr);
    m->wifi = wifi_state_from(&r->status, aw_busy(r->worker), r->wifi_exit, r->wanted_wifi);
    const char *note = r->settings_failed ? "Settings not saved" :
        r->action_note[0] ? r->action_note :
        r->settings.brightness_auto ? power_policy_note(r->policy) : "";
    snprintf(m->note, sizeof m->note, "%s", note);
    if (memcmp(&before, m, sizeof before)) m->dirty = 1;
}

int control_runtime_init(struct control_runtime *r, const char *path, int64_t now) {
    memset(r, 0, sizeof *r);
    r->settings_path = path;
    if (settings_load(&r->settings, path) < 0 && errno != ENOENT) r->settings_failed = 1;
    if (!settings_store_available(path)) r->settings_failed = 1;
    r->backlight_ready = backlight_open(&r->backlight) == 0;
    int max = r->backlight_ready ? r->backlight.max : 255;
    cc_init(&r->panel, r->settings.brightness, max,
            r->settings.brightness_auto, r->settings.sleep_minutes);
    r->policy = power_policy_new(max, now);
    r->worker = aw_new();
    if (!r->policy || !r->worker) { control_runtime_free(r); return -1; }
    sleep_timer_init(&r->idle, r->settings.sleep_minutes, now);
    read_status(r, now);
    wifi_read_ssid("/data/wifi.conf", r->panel.ssid, sizeof r->panel.ssid);
    struct utsname u;
    if (uname(&u) == 0) snprintf(r->panel.kernel, sizeof r->panel.kernel, "%.31s", u.release);
    FILE *f = fopen("/etc/taq102-build-id", "r");
    if (f) {
        if (fgets(r->panel.build, sizeof r->panel.build, f))
            r->panel.build[strcspn(r->panel.build, "\n")] = 0;
        fclose(f);
    }
    /* Until three fresh samples establish Auto, retain init's hardware level. */
    int initial = r->backlight_ready ? backlight_get(&r->backlight) : -1;
    apply(r, r->settings.brightness_auto && initial >= 8 ? initial : r->settings.brightness);
    readouts(r);
    return 0;
}

void control_runtime_tick(struct control_runtime *r, int64_t now, int asleep) {
    cc_tick(&r->panel, now);
    aw_poll(r->worker, &r->wifi_exit);
    if (now >= r->read_at_ms) read_status(r, now);
    if (!asleep && now >= r->policy_at_ms) {
        if (r->settings.brightness_auto) {
            /* Take this sample at the policy cadence, never reuse bar telemetry. */
            read_status(r, now);
            /* status_read stamps the sample with its own clock, a little after
             * this frame's `now`; judged against `now` the sample would look
             * like it came from the future and the policy would reset its
             * window every time. */
            int64_t at = r->status.read_ms > now ? r->status.read_ms : now;
            int level = power_policy_step(r->policy, &r->status, at, 0);
            if (level >= 8) apply(r, level);
        }
        r->policy_at_ms = now + 10000;
    }
    if (r->settings_pending && now >= r->save_at_ms) save(r);
    readouts(r);
    if (!asleep && sleep_timer_due(&r->idle, now, r->status.plugged, r->status.online_valid))
        r->sleep_requested = 1;
}

void control_runtime_action(struct control_runtime *r, struct cc_result result, int64_t now) {
    switch (result.action) {
    case CC_SET_BRIGHTNESS:
        r->settings.brightness = result.arg;
        apply(r, result.arg);
        r->settings_pending = 1; r->save_at_ms = now + 2000;
        break;
    case CC_SET_AUTO:
        r->settings.brightness_auto = result.arg;
        r->panel.auto_on = result.arg;
        /* Discard samples measured at a manual level, retain learned levels. */
        power_policy_step(r->policy, &r->status, now, 1);
        if (!result.arg && r->panel.dragging_brightness) {
            r->settings.brightness = r->panel.brightness;
            apply(r, r->panel.brightness);
        } else {
            apply(r, result.arg ? power_policy_level(r->policy) : r->settings.brightness);
        }
        r->policy_at_ms = now;
        r->settings_pending = 1; r->save_at_ms = now + 2000;
        break;
    case CC_SET_SLEEP_MINUTES:
        r->settings.sleep_minutes = result.arg;
        sleep_timer_set_minutes(&r->idle, result.arg, now);
        r->settings_pending = 1; r->save_at_ms = now + 2000;
        break;
    case CC_SLEEP_NOW: r->sleep_requested = 1; break;
    case CC_WIFI_TOGGLE: {
        const char *argv[] = {"/usr/sbin/taq102-wifi", result.arg ? "up" : "down", NULL};
        if (aw_start(r->worker, argv, 45) == 0) {
            r->wanted_wifi = result.arg; r->wifi_exit = 0;
            r->action_note[0] = 0;
        } else {
            snprintf(r->action_note, sizeof r->action_note, "Wi-Fi action unavailable");
        }
        break;
    }
    case CC_REBOOT_RESCUE:
        if (reboot_target_rescue(r->action_note, sizeof r->action_note) < 0)
            fprintf(stderr, "rescue: %s\n", r->action_note);
        break;
    case CC_REBOOT_LOADER:
        reboot_target_loader();
        snprintf(r->action_note, sizeof r->action_note, "Loader: %.38s", strerror(errno));
        fprintf(stderr, "%s\n", r->action_note);
        break;
    case CC_SAVE_SETTINGS: save(r); break;
    case CC_NONE: break;
    }
    readouts(r);
}

void control_runtime_sleep(struct control_runtime *r, int64_t now) {
    cc_close(&r->panel, now);
    r->panel.slide = r->panel.slide_from = 0;
    r->sleep_requested = 0;
    power_policy_step(r->policy, &r->status, now, 1);
    if (r->settings_pending) save(r);
}
void control_runtime_wake(struct control_runtime *r, int64_t now) {
    sleep_timer_touch(&r->idle, now);
    r->read_at_ms = r->policy_at_ms = now;
    apply(r, r->panel.brightness);
}
void control_runtime_free(struct control_runtime *r) {
    aw_free(r->worker);
    power_policy_free(r->policy);
}
