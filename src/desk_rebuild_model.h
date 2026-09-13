// A console refresh revalidates this show's controls without losing navigation.
#ifndef DESK_REBUILD_MODEL_H
#define DESK_REBUILD_MODEL_H

#include "desk_model.h"
#include "desk_layout_resolve.h"
#include "showmap.h"
#include "vcjson.h"

int desk_rebuild_model(struct desk_model *model, const struct show_map *map,
                       const struct vc_doc *console, const struct desk_layout *layout);

#endif
