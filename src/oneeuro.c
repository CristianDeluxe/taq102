#include "oneeuro.h"

#include <math.h>

// alpha for a first-order low pass, from a cutoff frequency and the interval
// actually measured between samples -- not a fixed one, because touch events
// do not arrive on a clock.
static float alpha(float cutoff, float dt) {
    float tau = 1.f / (2.f * (float)M_PI * cutoff);
    return 1.f / (1.f + tau / dt);
}

void oneeuro_init(struct oneeuro *f, float mincutoff, float beta) {
    f->mincutoff = mincutoff;
    f->beta = beta;
    f->dcutoff = 1.f;
    f->x_prev = f->dx_prev = f->t_prev = 0.f;
    f->started = 0;
}

float oneeuro_apply(struct oneeuro *f, float x, float t) {
    if (!f->started) {
        f->started = 1;
        f->x_prev = x;
        f->dx_prev = 0.f;
        f->t_prev = t;
        return x;
    }

    float dt = t - f->t_prev;
    if (dt <= 0.f || dt > 1.f) dt = 1.f / 60.f;   // a gap is not a velocity
    f->t_prev = t;

    // Speed, smoothed at a fixed cutoff: the cutoff for the position is driven
    // by this, so it must not be noisy itself.
    float dx = (x - f->x_prev) / dt;
    float ad = alpha(f->dcutoff, dt);
    float dx_hat = ad * dx + (1.f - ad) * f->dx_prev;
    f->dx_prev = dx_hat;

    float cutoff = f->mincutoff + f->beta * fabsf(dx_hat);
    float a = alpha(cutoff, dt);
    float x_hat = a * x + (1.f - a) * f->x_prev;
    f->x_prev = x_hat;

    return x_hat;
}
