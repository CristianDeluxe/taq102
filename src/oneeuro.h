// The 1€ filter: Casiez, Roussel and Vogel, CHI 2012.
//
// A touch controller's reported position shakes by a pixel or two even when the
// finger is still, and that shake is what makes a slow drag feel gritty. A
// plain low-pass filter removes it and adds lag, which is worse. This filter
// adapts instead: it smooths hard when the finger is barely moving, where
// jitter is visible and lag is not, and lets go when the finger moves fast,
// where lag is visible and jitter is not.
//
// Two knobs, and they are independent: lower `mincutoff` for less jitter when
// still, raise `beta` for less lag when fast.
#ifndef ONEEURO_H
#define ONEEURO_H

struct oneeuro {
    float mincutoff, beta, dcutoff;
    float x_prev, dx_prev, t_prev;
    int started;
};

void oneeuro_init(struct oneeuro *f, float mincutoff, float beta);

// `t` is a monotonic timestamp in seconds.
float oneeuro_apply(struct oneeuro *f, float x, float t);

#endif
