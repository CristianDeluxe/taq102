#include "wifi_join_finish.h"
#include "wifi_join_stage.h"

void wifi_join_finish(struct wifi_join *j) {
    j->running = 0;
    j->stage = STAGE_NONE;
    j->word[0] = '\0';
}
