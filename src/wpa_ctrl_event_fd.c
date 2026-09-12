#include "wpa_ctrl.h"
#include "wpa_ctrl_private.h"

int wpa_ctrl_event_fd(const struct wpa_ctrl *c) { return c ? c->event_fd : -1; }
