#include "desk_wifi_request.h"

enum desk_wifi_command desk_wifi_request_step(struct desk_wifi_request *r,
    struct wpa_ctrl *ctrl, int64_t now, int join_running, int surface_open,
    char *reply, size_t cap) {
    if (r->pending != DESK_WIFI_NONE) {
        // The reply is read before the deadline is judged: a busy loop that
        // arrives late must not call a waiting answer a timeout.
        r->result = wpa_ctrl_reply(ctrl, reply, cap);
        if (r->result == 0 && now < r->deadline_ms)
            return DESK_WIFI_NONE;
        if (r->result == 0)
            r->result = -1;
        if (r->result < 0)
            wpa_ctrl_abandon(ctrl);
        enum desk_wifi_command done = r->pending;
        r->pending = DESK_WIFI_NONE;
        return done;
    }
    if (join_running || !ctrl)
        return DESK_WIFI_NONE;
    const char *command;
    int timeout;
    if (r->results_queued) {
        r->results_queued = 0;
        r->pending = DESK_WIFI_RESULTS;
        command = "SCAN_RESULTS";
        timeout = 1000;
    } else if (r->scan_queued) {
        r->scan_queued = 0;
        r->pending = DESK_WIFI_SCAN;
        command = "SCAN";
        timeout = 1000;
    } else if (surface_open && now - r->last_status_ms >= 1000) {
        r->pending = DESK_WIFI_STATUS;
        r->last_status_ms = now;
        command = "STATUS";
        timeout = 500;
    } else {
        return DESK_WIFI_NONE;
    }
    r->deadline_ms = now + timeout;
    if (wpa_ctrl_begin(ctrl, command) == 0)
        return DESK_WIFI_NONE;
    r->result = -1;
    enum desk_wifi_command done = r->pending;
    r->pending = DESK_WIFI_NONE;
    return done;
}
