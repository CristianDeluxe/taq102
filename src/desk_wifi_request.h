#ifndef DESK_WIFI_REQUEST_H
#define DESK_WIFI_REQUEST_H

#include <stddef.h>
#include <stdint.h>

#include "wpa_ctrl.h"

enum desk_wifi_command { DESK_WIFI_NONE, DESK_WIFI_SCAN, DESK_WIFI_RESULTS, DESK_WIFI_STATUS };

struct desk_wifi_request {
    enum desk_wifi_command pending;
    int scan_queued, results_queued;
    int result; // 1 for a whole reply, -1 for timeout/send/receive failure
    int64_t deadline_ms, last_status_ms;
};

// Finish an outstanding card request even when a join is waiting to start.
// Otherwise schedule results, scan, then periodic status, only when the join
// does not own the socket. Returns the completed command, or NONE. The caller
// handles its reply and starts/steps joins only while pending is NONE.
enum desk_wifi_command desk_wifi_request_step(struct desk_wifi_request *r,
    struct wpa_ctrl *ctrl, int64_t now, int join_running, int surface_open,
    char *reply, size_t cap);

#endif
