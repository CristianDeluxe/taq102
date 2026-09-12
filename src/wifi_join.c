#include "wifi_join.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "action_worker.h"
#include "wifi_join_advance.h"
#include "wifi_join_event_matches.h"
#include "wifi_join_fail.h"
#include "wifi_join_finish.h"
#include "wifi_join_network_id.h"
#include "wifi_join_stage.h"
#include "wifi_join_start_renewal.h"
#include "wpa_ctrl.h"

enum wifi_join_state wifi_join_step(struct wifi_join *j, int64_t now_ms,
                                    const char *event, const char *address) {
    if (!j->running)
        return j->state;
    if (j->renew) {
        int status;
        aw_poll(j->renew, &status);
    }
    if (event && j->state == WIFI_JOIN_RUNNING && j->stage >= STAGE_SELECT &&
        j->stage <= STAGE_ENABLE && (j->stage != STAGE_SELECT || j->sent) && wifi_join_event_matches(j, event)) {
        if (strstr(event, "CTRL-EVENT-SSID-TEMP-DISABLED")) {
            wifi_join_fail(j, strstr(event, "WRONG_KEY") ? "Wrong key" : "Network refused the tablet", now_ms);
            return j->state;
        }
        // The event socket may become readable before SELECT_NETWORK's reply.
        if (strstr(event, "CTRL-EVENT-CONNECTED"))
            j->connected = 1;
    }
    if (j->stage == STAGE_ASSOC && j->connected) {
        if (wifi_join_start_renewal(j) != 0) {
            wifi_join_fail(j, "Cannot renew the lease", now_ms);
            return j->state;
        }
        wifi_join_advance(j, STAGE_ADDRESS, now_ms);
        snprintf(j->word, sizeof j->word, "Getting an address");
    }
    if (j->stage == STAGE_ADDRESS && address && address[0])
        wifi_join_advance(j, STAGE_ENABLE, now_ms);
    if (j->stage == STAGE_ASSOC || j->stage == STAGE_ADDRESS) {
        if (now_ms - j->stage_started_ms >= WIFI_JOIN_STAGE_MS)
            wifi_join_fail(j, j->stage == STAGE_ASSOC ? "No association" : "No address", now_ms);
        return j->state;
    }

    char cmd[48];
    const char *command;
    switch (j->stage) {
    case STAGE_RECONFIGURE: case STAGE_ROLLBACK_RECONFIGURE: command = "RECONFIGURE"; break;
    case STAGE_LIST: case STAGE_ROLLBACK_LIST: command = "LIST_NETWORKS"; break;
    case STAGE_SELECT: case STAGE_ROLLBACK_SELECT:
        snprintf(cmd, sizeof cmd, "SELECT_NETWORK %d", j->network_id);
        command = cmd;
        break;
    default: command = "ENABLE_NETWORK all"; break;
    }
    char reply[4096];
    int rc = 0;
    const char *error = NULL;
    if (!j->sent) {
        if (wpa_ctrl_begin(j->ctrl, command) != 0)
            error = "send failed";
        else {
            j->sent = 1;
            j->stage_started_ms = now_ms;
            return j->state;
        }
    } else if (now_ms - j->stage_started_ms >= WIFI_JOIN_REQUEST_MS) {
        error = "timed out";
    } else {
        rc = wpa_ctrl_reply(j->ctrl, reply, sizeof reply);
        if (rc == 0)
            return j->state;
        if (rc < 0)
            error = "reply failed";
        else {
            j->sent = 0;
            if (j->stage == STAGE_LIST || j->stage == STAGE_ROLLBACK_LIST) {
                j->network_id = wifi_join_network_id(reply, j->state == WIFI_JOIN_FAILED ? j->prev_ssid : j->ssid);
                if (j->network_id < 0)
                    error = "network missing";
            } else if (strcmp(reply, "OK\n") != 0 && strcmp(reply, "OK") != 0) {
                error = "refused";
            }
        }
    }
    if (error) {
        if (j->state == WIFI_JOIN_FAILED) {
            if (j->sent)
                wpa_ctrl_abandon(j->ctrl);
            j->sent = 0;
            size_t len = strlen(j->reason);
            snprintf(j->reason + len, sizeof j->reason - len, "; recovery %s", error);
            wifi_join_finish(j);
        } else {
            char reason[64];
            snprintf(reason, sizeof reason, "%s %s", command, error);
            wifi_join_fail(j, reason, now_ms);
        }
        return j->state;
    }
    switch (j->stage) {
    case STAGE_RECONFIGURE: wifi_join_advance(j, STAGE_LIST, now_ms); break;
    case STAGE_LIST: wifi_join_advance(j, STAGE_SELECT, now_ms); break;
    case STAGE_SELECT:
        wifi_join_advance(j, STAGE_ASSOC, now_ms);
        snprintf(j->word, sizeof j->word, "Associating");
        break;
    case STAGE_ENABLE:
        free(j->before);
        j->before = NULL;
        j->before_len = 0;
        j->wrote_block = 0;
        j->state = WIFI_JOIN_DONE;
        wifi_join_finish(j);
        break;
    case STAGE_ROLLBACK_RECONFIGURE:
        wifi_join_advance(j, j->prev_ssid[0] ? STAGE_ROLLBACK_LIST : STAGE_ROLLBACK_ENABLE, now_ms);
        break;
    case STAGE_ROLLBACK_LIST: wifi_join_advance(j, STAGE_ROLLBACK_SELECT, now_ms); break;
    default: wifi_join_finish(j); break;
    }
    return j->state;
}
