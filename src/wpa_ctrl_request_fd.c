#include "wpa_ctrl.h"
#include "wpa_ctrl_private.h"

int wpa_ctrl_request_fd(const struct wpa_ctrl *c) { return c ? c->fd : -1; }
