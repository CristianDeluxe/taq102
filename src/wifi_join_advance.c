#include "wifi_join_advance.h"

void wifi_join_advance(struct wifi_join *j, int stage, int64_t now) {
    j->stage = stage;
    j->sent = 0;
    j->stage_started_ms = now;
}
