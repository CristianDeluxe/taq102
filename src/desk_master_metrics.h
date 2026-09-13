#ifndef DESK_MASTER_METRICS_H
#define DESK_MASTER_METRICS_H

#define DESK_MASTER_THUMB_H 24
// Eight pixels inward from each end of the thumb centre's travel absorb
// finger placement error. Outside travel also saturates, so neither endpoint
// needs a one-pixel hit. Keep the interior scale unchanged to follow the grip.
#define DESK_MASTER_END_MARGIN 8

#endif
