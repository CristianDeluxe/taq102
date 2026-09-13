#ifndef DESK_INERT_MASK_H
#define DESK_INERT_MASK_H
#include "desk_model.h"
#include "desk_setup.h"
#include "desk_speed.h"
#include "desk_region_mark.h"
#include "desk_setup_inert_mask.h"
#include "desk_speed_inert_mask.h"
#include "desk_speed_paint.h"
void desk_inert_mask(const struct desk_model *m, const struct desk_setup *s, const struct desk_speed *speed, unsigned char *mask);
#endif
