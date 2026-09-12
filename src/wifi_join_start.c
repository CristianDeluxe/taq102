#include "wifi_join.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_conf.h"
#include "wifi_join_advance.h"
#include "wifi_join_restore.h"
#include "wifi_join_stage.h"

int wifi_join_start(struct wifi_join *j, const char *ssid, const char *psk, int known,
                    const char *prev_ssid, int64_t now_ms) {
    if (j->running) {
        return -1;
    }
    // A failed wifi_join_restore must never lose its only backup to a subsequent join.
    if (j->restore_failed && wifi_join_restore(j) != 0) {
        snprintf(j->reason, sizeof j->reason, "Config not restored; retry later");
        return -1;
    }
    j->reason[0] = '\0';
    if (!j->ctrl || !ssid || !ssid[0] || strlen(ssid) >= sizeof j->ssid) {
        snprintf(j->reason, sizeof j->reason, "Invalid network");
        return -1;
    }
    snprintf(j->ssid, sizeof j->ssid, "%s", ssid);
    snprintf(j->prev_ssid, sizeof j->prev_ssid, "%s", prev_ssid ? prev_ssid : "");
    struct wifi_conf conf;
    if (wifi_conf_read(j->conf_path, &conf) < 0) {
        snprintf(j->reason, sizeof j->reason, "Cannot read the config");
        return -1;
    }
    int write_block = !(known && wifi_conf_knows(&conf, ssid));
    free(j->before);
    j->before = NULL;
    j->before_len = 0;
    j->wrote_block = 0;
    if (write_block && wifi_conf_snapshot(j->conf_path, &j->before, &j->before_len) != 0) {
        snprintf(j->reason, sizeof j->reason, "Cannot read the config");
        return -1;
    }
    if (write_block && wifi_conf_write_block(j->conf_path, ssid, psk,
                                            wifi_conf_top_priority(&conf) + 1) != 0) {
        free(j->before);
        j->before = NULL;
        j->before_len = 0;
        snprintf(j->reason, sizeof j->reason, "Cannot write the config");
        return -1;
    }
    j->wrote_block = write_block;
    j->state = WIFI_JOIN_RUNNING;
    j->running = 1;
    j->connected = 0;
    j->network_id = -1;
    wifi_join_advance(j, STAGE_RECONFIGURE, now_ms);
    snprintf(j->word, sizeof j->word, "Configuring network");
    return 0;
}
