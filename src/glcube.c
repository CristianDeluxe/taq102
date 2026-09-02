// A lit, spinning cube on the TAQ-102 panel, rendered by the Mali-400 MP2
// through EGL/GLES2 on a GBM surface and scanned out by KMS page flips.
// Touch spins it. Same shape as particles: one process owning KMS, no X,
// no Wayland, no compositor -- but the pixels come from the GPU, not the CPU.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <sys/ioctl.h>

// The kernel's input_event on this 32-bit kernel is 16 bytes; a libc with
// 64-bit time_t describes it as 24 and a sizeof() check then never matches.
// Pin the wire format, as particles.c does.
struct kev { uint32_t sec, usec; uint16_t type, code; int32_t value; };

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "arcball.h"
#include "oneeuro.h"

static const char *VERT =
    "attribute vec3 pos;\n"
    "attribute vec3 nrm;\n"
    "attribute vec3 col;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 model;\n"
    "uniform mat4 modelview;\n"
    "varying vec3 v_col;\n"
    "varying vec3 v_nrm;\n"
    "varying vec3 v_eye;\n"
    "void main() {\n"
    "  v_col = col;\n"
    "  v_nrm = (model * vec4(nrm, 0.0)).xyz;\n"
    "  v_eye = (modelview * vec4(pos, 1.0)).xyz;\n"
    "  gl_Position = mvp * vec4(pos, 1.0);\n"
    "}\n";

// The panel is a cheap TN with a modest contrast ratio, so a physically
// plausible Lambert term reads as washed out on it. Wrap lighting keeps the
// unlit faces off the floor, a Blinn highlight gives the edges something to
// catch, and the final power lifts the midtones the way a display gamma of
// ~2.4 would otherwise crush them.
static const char *FRAG =
    "precision mediump float;\n"
    "varying vec3 v_col;\n"
    "varying vec3 v_nrm;\n"
    "varying vec3 v_eye;\n"
    "void main() {\n"
    "  vec3 n = normalize(v_nrm);\n"
    "  vec3 l = normalize(vec3(0.4, 0.7, 1.0));\n"
    "  vec3 v = normalize(-v_eye);\n"
    "  vec3 h = normalize(l + v);\n"
    "  float d = dot(n, l) * 0.5 + 0.5;\n"
    "  float s = pow(max(dot(n, h), 0.0), 24.0);\n"
    "  vec3 c = v_col * (0.30 + 0.85 * d * d) + vec3(0.55 * s);\n"
    "  gl_FragColor = vec4(pow(min(c, 1.0), vec3(0.80)), 1.0);\n"
    "}\n";

// Six faces, two triangles each: position, normal, colour per vertex.
#define F(nx, ny, nz, r, g, b, ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz) \
    ax, ay, az, nx, ny, nz, r, g, b,  bx, by, bz, nx, ny, nz, r, g, b, \
    cx, cy, cz, nx, ny, nz, r, g, b,  ax, ay, az, nx, ny, nz, r, g, b, \
    cx, cy, cz, nx, ny, nz, r, g, b,  dx, dy, dz, nx, ny, nz, r, g, b

static const GLfloat CUBE[] = {
    F( 0, 0, 1,  1.00f, 0.15f, 0.25f,  -1,-1, 1,   1,-1, 1,   1, 1, 1,  -1, 1, 1),
    F( 0, 0,-1,  0.10f, 0.55f, 1.00f,   1,-1,-1,  -1,-1,-1,  -1, 1,-1,   1, 1,-1),
    F( 1, 0, 0,  1.00f, 0.70f, 0.05f,   1,-1, 1,   1,-1,-1,   1, 1,-1,   1, 1, 1),
    F(-1, 0, 0,  0.15f, 0.95f, 0.35f,  -1,-1,-1,  -1,-1, 1,  -1, 1, 1,  -1, 1,-1),
    F( 0, 1, 0,  1.00f, 1.00f, 1.00f,  -1, 1, 1,   1, 1, 1,   1, 1,-1,  -1, 1,-1),
    F( 0,-1, 0,  0.55f, 0.25f, 1.00f,  -1,-1,-1,   1,-1,-1,   1,-1, 1,  -1,-1, 1),
};

static void mat_identity(float *m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

static void mat_mul(float *out, const float *a, const float *b) {
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            t[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] +
                           a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
    memcpy(out, t, sizeof t);
}

static void mat_perspective(float *m, float fovy, float aspect, float n, float f) {
    float t = 1.f / tanf(fovy * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = t / aspect; m[5] = t;
    m[10] = (f + n) / (n - f); m[11] = -1.f;
    m[14] = 2.f * f * n / (n - f);
}

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "shader: %s\n", log);
        exit(1);
    }
    return s;
}

static void on_flip(int fd, unsigned seq, unsigned s, unsigned us, void *data) {
    (void)fd; (void)seq; (void)s; (void)us;
    *(int *)data = 1;
}

// One scanout framebuffer per GBM buffer object, created once and kept for the
// object's life: the surface recycles a small ring, so this settles after two
// or three frames.
struct fbmap { struct gbm_bo *bo; uint32_t fb; };
static struct fbmap fbs[8];
static int nfbs;

static uint32_t fb_for(int fd, struct gbm_bo *bo) {
    for (int i = 0; i < nfbs; i++)
        if (fbs[i].bo == bo) return fbs[i].fb;
    uint32_t fb = 0;
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    if (drmModeAddFB(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo),
                     24, 32, stride, handle, &fb)) {
        perror("addfb");
        exit(1);
    }
    if (nfbs == (int)(sizeof fbs / sizeof fbs[0])) {
        fprintf(stderr, "too many gbm buffers\n");
        exit(1);
    }
    fbs[nfbs].bo = bo;
    fbs[nfbs].fb = fb;
    nfbs++;
    return fb;
}

int main(void) {
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open card0"); return 1; }

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) { perror("drmModeGetResources"); return 1; }

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) { conn = c; break; }
        if (c) drmModeFreeConnector(c);
    }
    if (!conn) { fprintf(stderr, "no connected connector\n"); return 1; }

    drmModeModeInfo mode = conn->modes[0];
    int W = mode.hdisplay, H = mode.vdisplay;
    drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
    uint32_t crtc_id = enc ? enc->crtc_id : res->crtcs[0];

    struct gbm_device *gbm = gbm_create_device(fd);
    if (!gbm) { fprintf(stderr, "gbm_create_device failed\n"); return 1; }
    printf("gbm backend: %s\n", gbm_device_get_backend_name(gbm));

    struct gbm_surface *surf = gbm_surface_create(
        gbm, W, H, GBM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!surf) { fprintf(stderr, "gbm_surface_create failed\n"); return 1; }

    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
    if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay failed\n"); return 1; }
    EGLint major, minor;
    if (!eglInitialize(dpy, &major, &minor)) { fprintf(stderr, "eglInitialize failed\n"); return 1; }
    printf("EGL %d.%d %s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));

    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) {
        fprintf(stderr, "eglChooseConfig failed\n"); return 1;
    }

    static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "eglCreateContext failed\n"); return 1; }

    EGLSurface egl_surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)surf, NULL);
    if (egl_surf == EGL_NO_SURFACE) { fprintf(stderr, "eglCreateWindowSurface failed\n"); return 1; }
    if (!eglMakeCurrent(dpy, egl_surf, egl_surf, ctx)) {
        fprintf(stderr, "eglMakeCurrent failed\n"); return 1;
    }
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    fflush(stdout);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, VERT));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, FRAG));
    glBindAttribLocation(prog, 0, "pos");
    glBindAttribLocation(prog, 1, "nrm");
    glBindAttribLocation(prog, 2, "col");
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        fprintf(stderr, "link: %s\n", log);
        return 1;
    }
    glUseProgram(prog);
    GLint u_mvp = glGetUniformLocation(prog, "mvp");
    GLint u_model = glGetUniformLocation(prog, "model");
    GLint u_modelview = glGetUniformLocation(prog, "modelview");

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof CUBE, CUBE, GL_STATIC_DRAW);
    const GLsizei stride = 9 * sizeof(GLfloat);
    for (int i = 0; i < 3; i++) {
        glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(uintptr_t)(i * 3 * sizeof(GLfloat)));
        glEnableVertexAttribArray(i);
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glViewport(0, 0, W, H);

    float proj[16];
    mat_perspective(proj, 45.f * (float)M_PI / 180.f, (float)W / (float)H, 1.f, 20.f);
    // The view is a fixed pull-back along -Z; the cube itself is what moves.
    float view[16];
    mat_identity(view);
    view[14] = -6.f;   // rewritten every frame once a pinch moves the camera

    int tfd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    int touching = 0;

    // Protocol B multitouch, and two things about it that are easy to get
    // wrong and were both got wrong here first:
    //
    // ABS_MT_SLOT is *state*, not a field of each event: the kernel emits it
    // only when the slot changes, so a reader that opens the device mid-stream
    // does not know which slot the positions it receives belong to. Assuming 0
    // parked a phantom contact in slot 0 that never lifted -- the driver never
    // selects slot 0, so it never sends that slot a tracking id of -1 -- and
    // every pinch then measured against a frozen point.
    //
    // A slot becomes active on a tracking id and only on a tracking id.
    // Positions carry no such meaning, and treating them as if they did is
    // what made the phantom stick.
    //
    // EVIOCGMTSLOTS asks the kernel for the current state at open, which is
    // the supported way to start from the truth rather than from a guess.
    //
    // This driver happens to number its slots from 1 -- one finger is slot 1,
    // two are slots 1 and 2 -- but nothing promises that, so the array is
    // indexed by slot number and no number is assumed.
    #define MAX_SLOTS 8
    struct slot {
        int active;
        float x, y;                  // filtered, which is what the gesture uses
        struct oneeuro fx, fy;
    } slots[MAX_SLOTS];
    memset(slots, 0, sizeof slots);
    // 1 Hz minimum cutoff kills the standing jitter; the speed term hands the
    // signal straight back as soon as a finger actually moves.
    for (int i = 0; i < MAX_SLOTS; i++) {
        oneeuro_init(&slots[i].fx, 1.0f, 0.02f);
        oneeuro_init(&slots[i].fy, 1.0f, 0.02f);
    }

    struct arcball ball;
    arcball_init(&ball);
    int cur_slot = -1;

    if (tfd >= 0) {
        int32_t q[1 + MAX_SLOTS];
        q[0] = ABS_MT_TRACKING_ID;
        if (ioctl(tfd, EVIOCGMTSLOTS(sizeof q), q) >= 0)
            for (int i = 0; i < MAX_SLOTS; i++)
                slots[i].active = (q[1 + i] >= 0);
    }
    float pinch_ref = 0.f;   // finger distance when the pinch started
    float dist_ref = 6.f;    // camera distance at that moment
    float prev_twist = 0.f;  // angle of the line between the fingers
    // This controller drops a contact for a frame or two in the middle of a
    // gesture -- measured, not assumed: the trace goes 2 slots, 1 slot, 2 slots
    // while both fingers stay on the glass. Ending the pinch on the first frame
    // that shows one finger means re-anchoring on the next, and the cube jumps.
    // So a pinch survives a short dropout.
    int lone_frames = 0;
    #define PINCH_GRACE 4
    // Two contacts a few pixels apart are the controller splitting one finger,
    // not a pinch, and anchoring on them scales wildly.
    #define PINCH_MIN_SPAN 40.f
    #define CAM_NEAR 3.2f
    #define CAM_FAR 14.f
    // Give it a gentle spin to start, so an untouched tablet is not a still
    // picture: a small rotation about a tilted axis, replayed by the coast.
    arcball_twist(&ball, 0.3f);
    ball.spin[0] = cosf(0.004f);
    ball.spin[1] = 0.35f * sinf(0.004f);
    ball.spin[2] = 0.90f * sinf(0.004f);
    ball.spin[3] = 0.25f * sinf(0.004f);
    float cam = 6.f;         // where the camera is now, along -Z

    struct gbm_bo *prev_bo = NULL;
    int first = 1;
    struct timespec t0 = {0, 0};
    long frames = 0;

    for (;;) {
        struct kev ev;
        while (tfd >= 0 && read(tfd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
            if (ev.type != EV_ABS) continue;
            // The filter needs the event's own timestamp: the interval between
            // touch samples is what sets how hard it smooths, and it is not the
            // frame interval.
            float evtime = (float)ev.sec + (float)ev.usec * 1e-6f;
            if (ev.code == ABS_MT_SLOT) {
                cur_slot = ev.value;
                continue;
            }
            // Until the first slot arrives there is nothing to attribute
            // positions to, and guessing is exactly the bug above.
            if (cur_slot < 0 || cur_slot >= MAX_SLOTS) continue;

            switch (ev.code) {
            case ABS_MT_TRACKING_ID:
                slots[cur_slot].active = (ev.value != -1);
                if (ev.value == -1) {
                    touching = 0;
                } else {
                    // A new contact is a new signal. The filter keeps its state
                    // per slot, and a slot gets reused: without this reset the
                    // finger appears to start where the *previous* finger in
                    // that slot ended and slides to where it really is, over
                    // the tenth of a second the adaptive cutoff needs to notice
                    // the jump. Two fingers closing then read as separating,
                    // and the cube grows while you pinch it smaller.
                    oneeuro_init(&slots[cur_slot].fx, 1.0f, 0.02f);
                    oneeuro_init(&slots[cur_slot].fy, 1.0f, 0.02f);
                }
                break;
            case ABS_MT_POSITION_X:
                slots[cur_slot].x =
                    oneeuro_apply(&slots[cur_slot].fx, ev.value, evtime);
                break;
            case ABS_MT_POSITION_Y:
                slots[cur_slot].y =
                    oneeuro_apply(&slots[cur_slot].fy, ev.value, evtime);
                break;
            default:
                break;
            }
        }

        // Whichever slots the driver happens to be using: the first two
        // active ones, in slot order.
        struct slot *a = NULL, *b = NULL;
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (!slots[i].active) continue;
            if (!a) a = &slots[i];
            else if (!b) { b = &slots[i]; break; }
        }

        if (a && b) {
            // Two fingers carry three independent measurements, and each one
            // drives exactly one thing: the distance between them is the zoom,
            // the point between them is the drag, and the angle of the line
            // joining them is the twist. Taking them apart this way is what
            // lets one movement do all three at once.
            float dx = a->x - b->x, dy = a->y - b->y;
            float d = sqrtf(dx * dx + dy * dy);
            float cx = (a->x + b->x) * 0.5f, cy = (a->y + b->y) * 0.5f;
            float twist = atan2f(dy, dx);

            lone_frames = 0;
            if (d > PINCH_MIN_SPAN) {
                if (pinch_ref == 0.f) {
                    pinch_ref = d;
                    dist_ref = cam;
                    prev_twist = twist;
                    arcball_begin(&ball, cx, cy, W, H);
                } else {
                    // atan2 wraps at pi; without unwrapping, one crossing would
                    // spin the cube half a turn in a single frame.
                    float dt = twist - prev_twist;
                    while (dt > (float)M_PI) dt -= 2.f * (float)M_PI;
                    while (dt < -(float)M_PI) dt += 2.f * (float)M_PI;
                    prev_twist = twist;

                    arcball_drag(&ball, cx, cy, W, H);
                    arcball_twist(&ball, dt);
                }
                cam = dist_ref * (pinch_ref / d);
                // Anti-windup. Held against a limit, the anchor would keep
                // integrating a zoom that cannot happen, and the fingers would
                // have to give all of it back before the cube moved again --
                // which is what "it gets stuck" was. Re-anchor at the limit so
                // the very next pixel in the other direction responds.
                if (cam < CAM_NEAR || cam > CAM_FAR) {
                    cam = cam < CAM_NEAR ? CAM_NEAR : CAM_FAR;
                    pinch_ref = d;
                    dist_ref = cam;
                }
            }
            touching = 0;   // a fresh single-finger drag starts on release
        } else if (a) {
            if (pinch_ref != 0.f && lone_frames++ < PINCH_GRACE) {
                // Probably a dropout, not a finger leaving: hold the pinch and
                // change nothing this frame.
            } else {
                pinch_ref = 0.f;
                if (!touching) { arcball_begin(&ball, a->x, a->y, W, H); touching = 1; }
                else arcball_drag(&ball, a->x, a->y, W, H);
            }
        } else {
            pinch_ref = 0.f;
            lone_frames = 0;
            touching = 0;
            arcball_end(&ball);
        }
        arcball_coast(&ball, 0.985f);
        view[14] = -cam;

        float model[16], mv[16], mvp[16];
        arcball_matrix(&ball, model);
        mat_mul(mv, view, model);
        mat_mul(mvp, proj, mv);

        glClearColor(0.07f, 0.08f, 0.13f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
        glUniformMatrix4fv(u_model, 1, GL_FALSE, model);
        glUniformMatrix4fv(u_modelview, 1, GL_FALSE, mv);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        eglSwapBuffers(dpy, egl_surf);

        struct gbm_bo *bo = gbm_surface_lock_front_buffer(surf);
        if (!bo) { fprintf(stderr, "lock_front_buffer failed\n"); return 1; }
        uint32_t fb = fb_for(fd, bo);

        if (first) {
            if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) {
                perror("setcrtc"); return 1;
            }
            printf("KMS up: %dx%d@%d on connector %u\n",
                   W, H, mode.vrefresh, conn->connector_id);
            fflush(stdout);
            first = 0;
        } else {
            int flip_done = 0;
            if (drmModePageFlip(fd, crtc_id, fb, DRM_MODE_PAGE_FLIP_EVENT, &flip_done) == 0) {
                drmEventContext evctx = { .version = 2, .page_flip_handler = on_flip };
                struct pollfd pfd = { .fd = fd, .events = POLLIN };
                while (!flip_done && poll(&pfd, 1, 100) > 0) drmHandleEvent(fd, &evctx);
            }
        }

        // Only release the previous buffer, never the one being scanned out.
        if (prev_bo) gbm_surface_release_buffer(surf, prev_bo);
        prev_bo = bo;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (t0.tv_sec == 0) t0 = now;
        frames++;
        double el = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
        if (el >= 1.0) {
            if (getenv("GLCUBE_TRACE")) {
                int n = 0;
                char which[64] = "";
                for (int i = 0; i < MAX_SLOTS; i++)
                    if (slots[i].active) {
                        n++;
                        snprintf(which + strlen(which), sizeof which - strlen(which),
                                 "%d(%.0f,%.0f) ", i, slots[i].x, slots[i].y);
                    }
                printf("%.1f FPS | slots %d: %s| cam %.2f pinch_ref %.0f dragging %d\n",
                       frames / el, n, which, cam, pinch_ref, ball.dragging);
            } else {
                printf("%.1f FPS\n", frames / el);
            }
            fflush(stdout);
            frames = 0; t0 = now;
        }
    }
    return 0;
}
