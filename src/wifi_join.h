// Joining a network as one transaction with a way back: the block is written
// to the config, the supplicant re-reads it and is told to select that
// network, association is awaited on the event socket, then the lease is
// renewed by a fixed worker command and an address is awaited. A wrong key,
// a timeout at any stage, or a refusal rolls back: the block is removed, the
// supplicant re-reads, and the previous network is selected again. The
// passphrase touches nothing but the config file.
#ifndef WIFI_JOIN_H
#define WIFI_JOIN_H

#include <stdint.h>

#include "action_worker.h"
#include "wifi_scan.h"
#include "wpa_ctrl.h"

#define WIFI_JOIN_STAGE_MS 20000

enum wifi_join_state { WIFI_JOIN_IDLE, WIFI_JOIN_RUNNING, WIFI_JOIN_DONE, WIFI_JOIN_FAILED };

struct wifi_join {
    enum wifi_join_state state;
    struct wpa_ctrl *ctrl;
    const char *conf_path;
    char ssid[WIFI_SSID_MAX];
    char prev_ssid[WIFI_SSID_MAX];
    int stage;                      // private
    int64_t stage_started_ms;
    char word[24];                  // "Associating", "Getting an address"
    char reason[48];                // why it failed, for the card
    struct action_worker *renew;
    const char *const *renew_argv;  // the lease renewal; NULL keeps the default
};

void wifi_join_init(struct wifi_join *j, struct wpa_ctrl *ctrl, const char *conf_path);

// Writes the block (psk NULL for an open network), reconfigures and selects.
// Returns 0 with the join running, -1 with `reason` set and nothing changed
// for the supplicant (a block that could not be written, a refusal).
int wifi_join_start(struct wifi_join *j, const char *ssid, const char *psk,
                    const char *prev_ssid, int64_t now_ms);

// Feeds one supplicant event line (or NULL) and the interface's current
// address (empty when none). Advances stages, runs timeouts, rolls back on
// failure. Returns the state after the step.
enum wifi_join_state wifi_join_step(struct wifi_join *j, int64_t now_ms,
                                    const char *event, const char *address);

void wifi_join_free(struct wifi_join *j);

#endif
