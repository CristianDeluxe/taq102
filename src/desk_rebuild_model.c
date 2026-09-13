#include "desk_rebuild_model.h"

#include <string.h>

#include "showmap_validate.h"

// The validator initializes the whole model. Keep navigation outside that reset
// on a console refresh, then restore through the same clamp as a finger uses.
// Installing a genuinely new show still uses desk_set_layout and starts at zero.
int desk_rebuild_model(struct desk_model *model, const struct show_map *map,
                       const struct vc_doc *console, const struct desk_layout *layout) {
    int page = model->page;
    int pages = model->layout.pages;
    int banks[MAP_MAX_PAGES];
    memcpy(banks, model->page_bank, sizeof banks);
    int enabled = showmap_build(model, map, console);
    desk_set_layout(model, layout);
    for (int i = 0; i < pages && i < layout->pages; i++)
        desk_set_view(model, i, banks[i]);
    desk_set_view(model, page, -1);
    return enabled;
}
