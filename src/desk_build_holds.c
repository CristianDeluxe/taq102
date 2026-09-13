#include "desk_build_holds.h"

#include <stdio.h>

#include "desk_burst_base.h"
#include "touch_input.h"

// Bursts, including fog, use the master's duration with no local cooldown;
// validated light holds use three seconds. A failed registration must paint
// unavailable, because a live tile with no hold slot cannot honour a touch.
void desk_build_holds(struct desk_hold *hold, struct desk_model *model, int *owner) {
    desk_hold_init(hold);
    for (int i = 0; i < model->count; i++) {
        struct desk_control *c = &model->control[i];
        c->hold_index = -1;
        if (c->kind == DESK_BURST && c->enabled) {
            // The master ends the burst at its length; the desk's own stop on
            // release or at the same cap is a belt over that brace.
            c->hold_index = desk_hold_add(hold, BURST_BASE + c->function_id, HOLD_HIT, c->burst_ms, 0);
        } else if (c->kind == DESK_HOLD && c->enabled) {
            // Three seconds for a light hit; strobes and fog never get here,
            // the validator keeps their held controls on the Mac.
            c->hold_index = desk_hold_add(hold, c->widget_id, HOLD_HIT, 3000, 0);
        } else {
            continue;
        }
        if (c->hold_index < 0) {
            c->enabled = 0;
            snprintf(c->reason, sizeof c->reason, "hold unavailable");
            fprintf(stderr, "desk: %s: %s\n", c->label, c->reason);
        }
    }
    for (int s = 0; s < TOUCH_MAX_SLOTS; s++)
        owner[s] = -1;
}
