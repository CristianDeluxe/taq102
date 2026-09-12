#include "wifi_join.h"
#include <stdlib.h>
#include "action_worker.h"
#include "wpa_ctrl.h"

void wifi_join_free(struct wifi_join *j) {
    if (j->sent)
        wpa_ctrl_abandon(j->ctrl);
    if (j->renew)
        aw_free(j->renew);
    j->renew = NULL;
    free(j->before);
    j->before = NULL;
}
