#include "wifi_join_start_renewal.h"
#include <stddef.h>
#include "action_worker.h"

int wifi_join_start_renewal(struct wifi_join *j) {
    static const char *const DEFAULT_RENEW[] = {
        "/bin/sh", "-c", "killall udhcpc 2>/dev/null; udhcpc -i wlan0 -b -x hostname:taq102", NULL,
    };
    if (!j->renew)
        j->renew = aw_new();
    if (!j->renew)
        return -1;
    const char *const *argv = j->renew_argv ? j->renew_argv : DEFAULT_RENEW;
    return aw_start(j->renew, argv, 30);
}
