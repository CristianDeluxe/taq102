#include "wifi_join.h"
#include <string.h>

void wifi_join_init(struct wifi_join *j, struct wpa_ctrl *ctrl, const char *conf_path) {
    memset(j, 0, sizeof *j);
    j->ctrl = ctrl;
    j->conf_path = conf_path;
    j->state = WIFI_JOIN_IDLE;
}
