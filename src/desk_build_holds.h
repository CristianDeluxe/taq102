// Register validated holds separately from the runtime so capacity failures
// exercise the same unavailable state on the host and on the tablet.
#ifndef DESK_BUILD_HOLDS_H
#define DESK_BUILD_HOLDS_H

#include "desk_hold.h"
#include "desk_model.h"

void desk_build_holds(struct desk_hold *hold, struct desk_model *model, int *owner);

#endif
