#include "wifi_join_fail.h"
#include <stdio.h>
#include <string.h>
#include "wifi_join_restore.h"
#include "wifi_join_advance.h"
#include "wifi_join_finish.h"
#include "wifi_join_stage.h"
#include "wpa_ctrl.h"

void wifi_join_fail(struct wifi_join *j, const char *reason, int64_t now) {
    if (j->sent)
        wpa_ctrl_abandon(j->ctrl);
    snprintf(j->reason, sizeof j->reason, "%s", reason);
    j->state = WIFI_JOIN_FAILED;
    wifi_join_advance(j, STAGE_RESTORE, now);
    snprintf(j->word, sizeof j->word, "Restoring network");
    // Restore immediately at the failed deadline. Daemon recovery remains
    // asynchronous, and retains socket ownership until running becomes false.
    if (wifi_join_restore(j) != 0) {
        size_t len = strlen(j->reason);
        snprintf(j->reason + len, sizeof j->reason - len, "; config not restored");
        wifi_join_finish(j);
    } else {
        wifi_join_advance(j, STAGE_ROLLBACK_RECONFIGURE, now);
    }
}
