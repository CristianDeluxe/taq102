#ifndef CONTROL_OVERLAY_H
#define CONTROL_OVERLAY_H
#include <stdint.h>
#include "control_center.h"
#include "status.h"
struct control_overlay;
struct control_overlay *control_overlay_new(int width, int height, const char *fonts);
/* Defer dirty work one frame after synchronous sysfs telemetry reads. */
void control_overlay_draw(struct control_overlay *overlay, struct cc_model *panel,
                          const struct status *status, int flipped, int64_t now, int defer_updates);
/* Counts panel texture updates; the independent status bar is excluded. */
unsigned control_overlay_take_uploads(struct control_overlay *overlay);
void control_overlay_free(struct control_overlay *overlay);
#endif
