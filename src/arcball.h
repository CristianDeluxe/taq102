// Trackball rotation: turn a drag across the glass into a rotation, by
// pretending there is a sphere behind the screen and rolling it under the
// finger. Ken Shoemake's ARCBALL (Graphics Gems IV, 1992), with the hyperbolic
// sheet outside the sphere that Holroyd's variant adds.
//
// Why this rather than "so many degrees per pixel": a gain constant has to be
// guessed, feels wrong at some speed or other, and turns a drag across the
// middle of the object and a drag around its edge into the same motion. Rolling
// a sphere has no constant to guess -- the finger stays on the point it grabbed
// -- and dragging near the edge twists, which is what the hand expects.
#ifndef ARCBALL_H
#define ARCBALL_H

struct arcball {
    float q[4];        // current orientation, (w, x, y, z)
    float last[3];     // where the drag last touched the sphere
    float spin[4];     // last frame's rotation, replayed as momentum
    int dragging;
};

void arcball_init(struct arcball *a);

// Screen pixel -> unit vector on the sphere.
void arcball_project(float px, float py, int w, int h, float *v);

// Start, continue and end a drag. Coordinates are in screen pixels.
void arcball_begin(struct arcball *a, float px, float py, int w, int h);
void arcball_drag(struct arcball *a, float px, float py, int w, int h);
void arcball_end(struct arcball *a);

// Roll about the view axis, for the twist of a two-finger gesture.
void arcball_twist(struct arcball *a, float radians);

// Keep spinning after release, decaying. Call once per frame.
void arcball_coast(struct arcball *a, float decay);

// Column-major 4x4 for the shader.
void arcball_matrix(const struct arcball *a, float *m);

#endif
