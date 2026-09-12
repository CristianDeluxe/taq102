#include "wifi_join.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wifi_conf.h"

enum { STAGE_NONE, STAGE_ASSOC, STAGE_ADDRESS };

#define REQUEST_MS 3000

static const char *const DEFAULT_RENEW[] = {
    "/bin/sh", "-c", "killall udhcpc 2>/dev/null; udhcpc -i wlan0 -b -x hostname:taq102", NULL,
};

static int ask_ok(struct wifi_join *j, const char *cmd) {
    char buf[64];
    int n = wpa_ctrl_request(j->ctrl, cmd, buf, sizeof buf, REQUEST_MS);
    return n >= 2 && strncmp(buf, "OK", 2) == 0;
}

// The id the supplicant gave `ssid`, from LIST_NETWORKS, or -1.
static int network_id(struct wifi_join *j, const char *ssid) {
    char buf[4096];
    int n = wpa_ctrl_request(j->ctrl, "LIST_NETWORKS", buf, sizeof buf, REQUEST_MS);
    if (n <= 0)
        return -1;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(line, '\t');
        if (!tab)
            continue;
        *tab = '\0';
        char *name = tab + 1;
        char *end = strchr(name, '\t');
        if (end)
            *end = '\0';
        if (strcmp(name, ssid) == 0)
            return atoi(line);
    }
    return -1;
}

static int select_network(struct wifi_join *j, const char *ssid) {
    int id = network_id(j, ssid);
    if (id < 0)
        return -1;
    char cmd[32];
    snprintf(cmd, sizeof cmd, "SELECT_NETWORK %d", id);
    return ask_ok(j, cmd) ? 0 : -1;
}

void wifi_join_init(struct wifi_join *j, struct wpa_ctrl *ctrl, const char *conf_path) {
    memset(j, 0, sizeof *j);
    j->ctrl = ctrl;
    j->conf_path = conf_path;
    j->state = WIFI_JOIN_IDLE;
}

static void fail(struct wifi_join *j, const char *reason) {
    snprintf(j->reason, sizeof j->reason, "%s", reason);
    j->state = WIFI_JOIN_FAILED;
    j->stage = STAGE_NONE;
    j->word[0] = '\0';
    // The way back: the block goes, the supplicant re-reads, and the previous
    // network is selected again so the tablet returns to where it was.
    // The file goes back to what it was: a replaced block returns whole,
    // key and all, and a new one disappears.
    if (j->wrote_block) {
        int rc = j->before || j->before_len == 0
               ? wifi_conf_restore(j->conf_path, j->before, j->before_len)
               : wifi_conf_remove(j->conf_path, j->ssid);
        if (rc != 0) {
            // The backup stays for the next attempt; the card says the file is not as it was.
            snprintf(j->reason, sizeof j->reason, "%s; config not restored", reason);
            fprintf(stderr, "wifi: cannot restore %s after a failed join\n", j->conf_path);
        } else {
            free(j->before);
            j->before = NULL;
            j->before_len = 0;
        }
    }
    ask_ok(j, "RECONFIGURE");
    if (j->prev_ssid[0])
        select_network(j, j->prev_ssid);
    else
        ask_ok(j, "ENABLE_NETWORK all");
}

int wifi_join_start(struct wifi_join *j, const char *ssid, const char *psk, int known,
                    const char *prev_ssid, int64_t now_ms) {
    if (j->state == WIFI_JOIN_RUNNING) {
        snprintf(j->reason, sizeof j->reason, "A join is running");
        return -1;
    }
    j->reason[0] = '\0';
    snprintf(j->ssid, sizeof j->ssid, "%s", ssid);
    snprintf(j->prev_ssid, sizeof j->prev_ssid, "%s", prev_ssid ? prev_ssid : "");
    struct wifi_conf conf;
    int priority = 1;
    if (wifi_conf_read(j->conf_path, &conf) >= 0)
        priority = wifi_conf_top_priority(&conf) + 1;
    // A known network keeps the block it has: its key is on file and stays.
    j->wrote_block = !(known && wifi_conf_knows(&conf, ssid));
    free(j->before);
    j->before = NULL;
    j->before_len = 0;
    if (j->wrote_block && wifi_conf_snapshot(j->conf_path, &j->before, &j->before_len) != 0) {
        snprintf(j->reason, sizeof j->reason, "Cannot read the config");
        return -1;
    }
    if (j->wrote_block && wifi_conf_write_block(j->conf_path, ssid, psk, priority) != 0) {
        snprintf(j->reason, sizeof j->reason, "Cannot write the config");
        return -1;
    }
    if (!ask_ok(j, "RECONFIGURE")) {
        if (j->wrote_block)
            wifi_conf_restore(j->conf_path, j->before, j->before_len);
        snprintf(j->reason, sizeof j->reason, "Supplicant refused the config");
        return -1;
    }
    if (select_network(j, ssid) != 0) {
        if (j->wrote_block)
            wifi_conf_restore(j->conf_path, j->before, j->before_len);
        ask_ok(j, "RECONFIGURE");
        snprintf(j->reason, sizeof j->reason, "Supplicant refused the network");
        return -1;
    }
    j->state = WIFI_JOIN_RUNNING;
    j->stage = STAGE_ASSOC;
    j->stage_started_ms = now_ms;
    snprintf(j->word, sizeof j->word, "Associating");
    return 0;
}

static int start_renewal(struct wifi_join *j) {
    if (!j->renew)
        j->renew = aw_new();
    if (!j->renew)
        return -1;
    const char *const *argv = j->renew_argv ? j->renew_argv : DEFAULT_RENEW;
    return aw_start(j->renew, argv, 30);
}

enum wifi_join_state wifi_join_step(struct wifi_join *j, int64_t now_ms,
                                    const char *event, const char *address) {
    if (j->state != WIFI_JOIN_RUNNING)
        return j->state;
    if (j->renew) {
        int status;
        aw_poll(j->renew, &status);
    }
    if (event) {
        // The supplicant names a bad key by disabling the network with the
        // reason; anything else it disables is a network that will not have
        // the tablet.
        if (strstr(event, "CTRL-EVENT-SSID-TEMP-DISABLED")) {
            fail(j, strstr(event, "WRONG_KEY") ? "Wrong key" : "Network refused the tablet");
            return j->state;
        }
        if (j->stage == STAGE_ASSOC && strstr(event, "CTRL-EVENT-CONNECTED")) {
            if (start_renewal(j) != 0) {
                fail(j, "Cannot renew the lease");
                return j->state;
            }
            j->stage = STAGE_ADDRESS;
            j->stage_started_ms = now_ms;
            snprintf(j->word, sizeof j->word, "Getting an address");
        }
    }
    if (j->stage == STAGE_ADDRESS && address && address[0]) {
        free(j->before);
        j->before = NULL;
        j->before_len = 0;
        j->state = WIFI_JOIN_DONE;
        j->stage = STAGE_NONE;
        j->word[0] = '\0';
        // Every known network is a candidate again; priority keeps this one first.
        ask_ok(j, "ENABLE_NETWORK all");
        return j->state;
    }
    if (now_ms - j->stage_started_ms > WIFI_JOIN_STAGE_MS)
        fail(j, j->stage == STAGE_ASSOC ? "No association" : "No address");
    return j->state;
}

void wifi_join_free(struct wifi_join *j) {
    if (j->renew)
        aw_free(j->renew);
    j->renew = NULL;
    free(j->before);
    j->before = NULL;
}
