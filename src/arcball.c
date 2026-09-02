#include "arcball.h"

#include <math.h>
#include <string.h>

static void quat_mul(float *out, const float *a, const float *b) {
    float r[4];
    r[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    r[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    r[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    r[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
    memcpy(out, r, sizeof r);
}

static void quat_normalize(float *q) {
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n < 1e-8f) { q[0] = 1.f; q[1] = q[2] = q[3] = 0.f; return; }
    for (int i = 0; i < 4; i++) q[i] /= n;
}

// The rotation that carries p onto q, along the great-circle arc between them.
// The angle is taken from the dot product rather than from the cross product
// alone, so the object turns by exactly the arc the finger swept: half of it
// and the sphere would lag the finger, twice and it would run ahead.
static void quat_between(float *out, const float *p, const float *q) {
    float n[3] = {
        p[1] * q[2] - p[2] * q[1],
        p[2] * q[0] - p[0] * q[2],
        p[0] * q[1] - p[1] * q[0],
    };
    float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl < 1e-6f) {          // the two points coincide: no rotation to build
        out[0] = 1.f; out[1] = out[2] = out[3] = 0.f;
        return;
    }
    float dot = p[0] * q[0] + p[1] * q[1] + p[2] * q[2];
    if (dot > 1.f) dot = 1.f;
    if (dot < -1.f) dot = -1.f;

    float half = acosf(dot) * 0.5f;
    float s = sinf(half) / nl;
    out[0] = cosf(half);
    out[1] = n[0] * s;
    out[2] = n[1] * s;
    out[3] = n[2] * s;
}

void arcball_init(struct arcball *a) {
    memset(a, 0, sizeof *a);
    a->q[0] = 1.f;
    a->spin[0] = 1.f;
}

// Outside the sphere the naive projection would clamp z to zero and the
// rotation would stop dead at the rim. Shoemake's sphere is therefore joined to
// a hyperbolic sheet at r/sqrt(2), where the two surfaces meet with the same
// slope, so a drag off the edge keeps turning smoothly instead of sticking.
void arcball_project(float px, float py, int w, int h, float *v) {
    float s = (float)(w < h ? w : h) - 1.f;
    float x = (2.f * px - (float)w + 1.f) / s;
    float y = -(2.f * py - (float)h + 1.f) / s;
    float d2 = x * x + y * y;
    float z = d2 <= 0.5f ? sqrtf(1.f - d2) : 0.5f / sqrtf(d2);

    float len = sqrtf(x * x + y * y + z * z);
    v[0] = x / len;
    v[1] = y / len;
    v[2] = z / len;
}

void arcball_begin(struct arcball *a, float px, float py, int w, int h) {
    arcball_project(px, py, w, h, a->last);
    a->dragging = 1;
    a->spin[0] = 1.f; a->spin[1] = a->spin[2] = a->spin[3] = 0.f;
}

void arcball_drag(struct arcball *a, float px, float py, int w, int h) {
    if (!a->dragging) { arcball_begin(a, px, py, w, h); return; }

    float now[3], delta[4];
    arcball_project(px, py, w, h, now);
    quat_between(delta, a->last, now);

    quat_mul(a->q, delta, a->q);
    quat_normalize(a->q);
    memcpy(a->spin, delta, sizeof delta);
    memcpy(a->last, now, sizeof now);
}

void arcball_end(struct arcball *a) {
    a->dragging = 0;
}

void arcball_twist(struct arcball *a, float radians) {
    float half = radians * 0.5f;
    float t[4] = { cosf(half), 0.f, 0.f, sinf(half) };   // about the view axis
    quat_mul(a->q, t, a->q);
    quat_normalize(a->q);
}

// Momentum is the last frame's rotation replayed with its angle scaled down a
// little each frame -- a quaternion slerped toward identity, which is what
// keeps the axis of the spin and only bleeds off its speed.
void arcball_coast(struct arcball *a, float decay) {
    if (a->dragging) return;

    float w = a->spin[0];
    if (w > 1.f) w = 1.f;
    if (w < -1.f) w = -1.f;
    float half = acosf(w);
    if (half < 1e-4f) return;

    float axis[3] = { a->spin[1], a->spin[2], a->spin[3] };
    float al = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (al < 1e-6f) return;

    half *= decay;
    float s = sinf(half) / al;
    float step[4] = { cosf(half), axis[0] * s, axis[1] * s, axis[2] * s };

    quat_mul(a->q, step, a->q);
    quat_normalize(a->q);
    memcpy(a->spin, step, sizeof step);
}

void arcball_matrix(const struct arcball *a, float *m) {
    float w = a->q[0], x = a->q[1], y = a->q[2], z = a->q[3];

    m[0]  = 1.f - 2.f * (y * y + z * z);
    m[1]  =       2.f * (x * y + z * w);
    m[2]  =       2.f * (x * z - y * w);
    m[3]  = 0.f;

    m[4]  =       2.f * (x * y - z * w);
    m[5]  = 1.f - 2.f * (x * x + z * z);
    m[6]  =       2.f * (y * z + x * w);
    m[7]  = 0.f;

    m[8]  =       2.f * (x * z + y * w);
    m[9]  =       2.f * (y * z - x * w);
    m[10] = 1.f - 2.f * (x * x + y * y);
    m[11] = 0.f;

    m[12] = m[13] = m[14] = 0.f;
    m[15] = 1.f;
}
