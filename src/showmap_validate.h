// Turns a parsed map plus the console the master actually has loaded into the
// desk's controls. A control the console cannot back is added disabled, with
// the reason written where the tile will show it, rather than dropped: an
// operator who sees "not in this show" knows more than one who sees a gap.
#ifndef SHOWMAP_VALIDATE_H
#define SHOWMAP_VALIDATE_H

#include "desk_model.h"
#include "showmap.h"
#include "vcjson.h"

// Fills `model` from `map`, checking every control against `doc`. Returns the
// number of controls that came out enabled.
int showmap_build(struct desk_model *model, const struct show_map *map,
                  const struct vc_doc *doc);

// Whether the console is another show than the map's: another schema on
// this side or another QLC+ line on the master's. Every performance control,
// the speed cards included, is gated on it.
int showmap_mismatch(const struct show_map *map, const struct vc_doc *doc);

// Where the chrome controls land in the model after the map's: the master,
// the panic button, SHOW's ambient OFF and its tempo card.
#define DESK_MASTER_INDEX(map) ((map)->count)
#define DESK_PANIC_INDEX(map) ((map)->count + 1)
#define DESK_HAZE_OFF_INDEX(map) ((map)->count + 2)
#define DESK_TEMPO_INDEX(map) ((map)->count + 3)

#endif
