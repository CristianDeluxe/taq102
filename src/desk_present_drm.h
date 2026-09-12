// The panel: two DRM dumb buffers, a mode set once, and a page flip per
// frame. Nothing else in the desk knows about DRM.
#ifndef DESK_PRESENT_DRM_H
#define DESK_PRESENT_DRM_H

#include "canvas.h"

struct present;

// Opens the card, takes the master, picks the connected connector and its
// first mode, and sets it. NULL on failure, with the reason on stderr.
struct present *present_open(const char *card);
int present_width(const struct present *p);
int present_height(const struct present *p);

// Copies the canvas into the buffer that is not on screen and flips to it,
// waiting for the flip to complete. Returns 0, or -1 if the flip failed.
//
// The whole canvas is copied every time. Damage tracking has to be per buffer
// -- a rectangle painted into one buffer is still stale in the other -- and
// until that is measured to be necessary, copying 2.4 MB is the version that
// cannot show a stale pixel.
int present_frame(struct present *p, const struct canvas *canvas);
// Copies only `rect` into the back buffer, plus whatever that buffer missed
// while the other was on screen, and flips. A negative width means the whole
// canvas. Damage is remembered per buffer so a stale pixel cannot come back.
int present_frame_damage(struct present *p, const struct canvas *canvas,
                         int x, int y, int w, int h);

void present_close(struct present *p);

#endif
