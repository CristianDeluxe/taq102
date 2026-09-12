#include "wifi_join_restore.h"
#include <stdlib.h>
#include "wifi_conf.h"

int wifi_join_restore(struct wifi_join *j) {
    if (!j->wrote_block)
        return 0;
    if (wifi_conf_restore(j->conf_path, j->before, j->before_len) != 0) {
        j->restore_failed = 1;
        return -1;
    }
    j->wrote_block = 0;
    j->restore_failed = 0;
    free(j->before);
    j->before = NULL;
    j->before_len = 0;
    return 0;
}
