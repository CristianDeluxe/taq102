// A lit, spinning cube on the TAQ-102 panel, rendered by the Mali-400 MP2
// through EGL/GLES2 on a GBM surface and scanned out by KMS page flips.
// Touch spins it. Same shape as particles: one process owning KMS, no X,
// no Wayland, no compositor -- but the pixels come from the GPU, not the CPU.
//
// The power button puts the tablet to sleep and wakes it, the way Android
// did: the panel and the backlight go off, the touch controller is put to
// rest, rendering stops, and the next press brings it all back. Wi-Fi and
// ssh stay up. Along the top, the status bar from statusbar.c. The
// accelerometer turns the picture round when the tablet is held the other
// way up: this is a landscape device, so there are two orientations, and
// gravity along the short axis of the screen picks one.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <sys/ioctl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "arcball.h"
#include "control_input.h"
#include "control_overlay.h"
#include "control_runtime.h"
#include "display_power.h"
#include "power_key.h"
#include "sleep_state.h"
#include "status.h"
#include "accel_monitor.h"
#include "touch_flip.h"

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

#define IDLE_HALF 0.004f    // half-angle per frame of the resting spin

static void idle_spin(struct arcball *b) {
    b->spin[0] = cosf(IDLE_HALF);
    b->spin[1] = 0.35f * sinf(IDLE_HALF);
    b->spin[2] = 0.90f * sinf(IDLE_HALF);
    b->spin[3] = 0.25f * sinf(IDLE_HALF);
}

int main(void) {
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open card0"); return 1; }
    if (drmSetMaster(fd) != 0) {
        fprintf(stderr, "drmSetMaster: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

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

    int trace = getenv("GLCUBE_TRACE") != NULL;
    int finish = getenv("GLCUBE_FINISH") != NULL;
    int static_scene = getenv("GLCUBE_STATIC") != NULL;

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

    const char *fonts = getenv("GLCUBE_FONTS");
    struct control_overlay *overlay = control_overlay_new(W, H,
        fonts ? fonts : "/usr/share/fonts/taq102");
    if (!overlay) { fprintf(stderr, "overlay initialization failed\n"); return 1; }
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);
    const char *settings = getenv("GLCUBE_SETTINGS");
    struct control_runtime controls;
    if (control_runtime_init(&controls, settings ? settings : "/data/taq102.conf",
        (int64_t)started.tv_sec * 1000 + started.tv_nsec / 1000000) < 0) {
        fprintf(stderr, "control initialization failed\n"); return 1;
    }

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

    const char *touch_path = getenv("GLCUBE_TOUCH");
    int tfd = open(touch_path ? touch_path : "/dev/input/event1", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (tfd < 0) perror("touch open");
    int touching = 0;
    // The PMIC's power button, KEY_POWER on press (1) and release (0).
    const char *power_path = getenv("GLCUBE_POWER");
    int pfd = open(power_path ? power_path : "/dev/input/event0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (pfd < 0) perror("power open");
    struct power_key power_key = {0};
    int waking = 0;      // the modeset that wakes the panel is pending

    // Orientation. The SC7A20's Y axis runs along the short side of the
    // screen: +1 g when the tablet is held the other way up, -1 g the usual
    // way (measured 2026-09-04 with the picture upside down: Y = +966 mg).
    // Half a g of hysteresis and three agreeing samples, so a tablet lying
    // flat or being turned does not flap.
    struct accel_monitor *accelerometer = getenv("GLCUBE_NOACCEL") ? NULL :
                                          accel_monitor_start();
    unsigned accel_sequence = 0;
    long accel_max_read_us = 0;
    int flipped = 0, flip_votes = 0;
    printf("accelerometer: %s\n", accelerometer ? "background reader" : "disabled");

    int declared_x = 0, declared_y = 0;
    if (tfd >= 0) {
        struct input_absinfo ai;
        if (ioctl(tfd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0) declared_x = ai.maximum;
        if (ioctl(tfd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0) declared_y = ai.maximum;
    }
    struct touch_flip touch_flip;
    const char *touch_flip_mode = getenv("GLCUBE_TOUCH_FLIP");
    if (touch_flip_configure(&touch_flip, W, H, touch_flip_mode) != 0)
        fprintf(stderr, "invalid GLCUBE_TOUCH_FLIP=%s; using xy\n", touch_flip_mode);
    printf("touch: declared %dx%d, mapping %dx%d, flip %s\n",
           declared_x, declared_y, W, H, touch_flip.name);
    printf("diagnostics: finish %s, static %s, trace %s\n",
           finish ? "on" : "off", static_scene ? "on" : "off",
           trace ? "on" : "off");

    struct control_input input;
    if (control_input_init(&input, &touch_flip) < 0) {
        fprintf(stderr, "touch initialization failed\n"); return 1;
    }
    struct arcball ball;
    arcball_init(&ball);
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
    // The coast bleeds 1.5% of the angle off per frame, so this alone stops
    // in five seconds -- measured 2026-09-04, a tablet up for 45 minutes with
    // the cube dead still and glcube at 54.8 FPS. Whenever no finger is down
    // and the coast has fallen to the resting rate, the spin is held there.
    // The first version let it fall to half the rate before topping it up,
    // which is a kick every 0.84 s, and I saw the cube "wobble a
    // millimetre every second".
    arcball_twist(&ball, 0.3f);
    if (static_scene) {
        ball.spin[0] = 1.f;
        ball.spin[1] = ball.spin[2] = ball.spin[3] = 0.f;
    } else {
        idle_spin(&ball);
    }
    float cam = 6.f;         // where the camera is now, along -Z

    struct gbm_bo *prev_bo = NULL;
    int first = 1;
    struct timespec t0 = {0, 0};
    struct timespec last_frame = {0, 0};
    double max_frame_ms = 0.0;
    long frames = 0;

    for (;;) {
        struct timespec frame_start;
        clock_gettime(CLOCK_MONOTONIC, &frame_start);
        if (last_frame.tv_sec != 0) {
            double frame_ms = (frame_start.tv_sec - last_frame.tv_sec) * 1000.0 +
                              (frame_start.tv_nsec - last_frame.tv_nsec) / 1e6;
            if (frame_ms > max_frame_ms) max_frame_ms = frame_ms;
        }
        last_frame = frame_start;

        int64_t now_ms = (int64_t)frame_start.tv_sec * 1000 + frame_start.tv_nsec / 1000000;
        double now_seconds = (double)frame_start.tv_sec + frame_start.tv_nsec / 1e9;
        int64_t previous_read_at_ms = controls.read_at_ms;
        int previously_armed = controls.panel.armed;
        if (power_key_read(&power_key, pfd) && !first) controls.sleep_requested = 1;
        if (control_input_read(&input, &controls, tfd, now_seconds) < 0) {
            perror("touch read"); return 1;
        }
        if (input.cube_released) touching = 0;
        control_runtime_tick(&controls, now_ms, 0);
        if (trace && previously_armed && !controls.panel.armed) {
            printf("control centre: disarmed\n"); fflush(stdout);
        }
        touch_router_set_panel_open(input.router, cc_visible(&controls.panel));
        if (controls.sleep_requested && !first) {
            control_runtime_sleep(&controls, now_ms);
            control_input_reset(&input, &controls, flipped);
            touching = 0; pinch_ref = 0; lone_frames = 0;
            arcball_end(&ball);
            if (display_power_off() < 0) fprintf(stderr, "sleep: power write failed\n");
            if (drmModeSetCrtc(fd, crtc_id, 0, 0, 0, NULL, 0, NULL)) {
                perror("sleep CRTC"); display_power_on(); return 1;
            }
            struct pickup pickup;
            pickup_start(&pickup, now_ms);
            unsigned sleep_sequence = accel_sequence;
            printf("sleep\n"); fflush(stdout);
            for (;;) {
                struct pollfd pp[2] = {{pfd, POLLIN, 0}, {tfd, POLLIN, 0}};
                int ready = poll(pp, 2, 250);
                if (ready < 0 && errno != EINTR) { perror("sleep poll"); break; }
                int wake = power_key_read(&power_key, pfd);
                /* Decode and discard while dark; preserve the decoder's fd/clock. */
                struct touch_event discarded[64];
                int n;
                do { n = tfd >= 0 ? touch_input_read_fd(input.decoder, tfd, discarded, 64) : 0; } while (n == 64);
                if (n < 0) { perror("sleep touch read"); break; }
                struct timespec current;
                clock_gettime(CLOCK_MONOTONIC, &current);
                now_ms = (int64_t)current.tv_sec * 1000 + current.tv_nsec / 1000000;
                control_runtime_tick(&controls, now_ms, 1);
                struct accel_snapshot sample;
                if (accelerometer && accel_monitor_snapshot(accelerometer, &sample) == 0 &&
                    sample.sequence != sleep_sequence) {
                    sleep_sequence = sample.sequence;
                    if (pickup_feed(&pickup, sample.x_mg, sample.y_mg, sample.z_mg, sample.at_ms)) wake = 1;
                }
                if (wake) break;
            }
            control_input_reset(&input, &controls, flipped);
            sleep_timer_touch(&controls.idle, now_ms);
            first = 1; waking = 1;
            t0.tv_sec = last_frame.tv_sec = 0;
            max_frame_ms = 0; frames = 0;
            control_overlay_take_uploads(overlay);
            printf("wake\n"); fflush(stdout);
        }

        if (!static_scene && accelerometer && frames % 10 == 0) {
            struct accel_snapshot sample;
            if (accel_monitor_snapshot(accelerometer, &sample) == 0 &&
                sample.sequence != accel_sequence) {
                accel_sequence = sample.sequence;
                accel_max_read_us = sample.max_read_us;
                int want = sample.y_mg > 500 ? 1 : sample.y_mg < -500 ? 0 : flipped;
                if (want != flipped && ++flip_votes >= 3) {
                    flipped = want;
                    flip_votes = 0;
                    control_input_reset(&input, &controls, flipped);
                    touching = 0; pinch_ref = 0; lone_frames = 0;
                    arcball_end(&ball);
                    printf("orientation: %s (y %d mg)\n",
                           flipped ? "turned round" : "normal", sample.y_mg);
                    fflush(stdout);
                } else if (want == flipped) {
                    flip_votes = 0;
                }
            }
        }

        if (!static_scene) {
            // Whichever slots the driver happens to be using: the first two
            // active ones, in slot order.
            const typeof(input.cube.slot[0]) *a = NULL, *b = NULL;
            for (int i = 0; i < TOUCH_MAX_SLOTS; i++) {
                if (!input.cube.slot[i].active) continue;
                if (!a) a = &input.cube.slot[i];
                else if (!b) { b = &input.cube.slot[i]; break; }
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
            if (!touching && !ball.dragging) {
                float w = ball.spin[0];
                if (w > 1.f) w = 1.f;
                if (acosf(w) <= IDLE_HALF) idle_spin(&ball);
            }
        }
        view[14] = -cam;

        float model[16], mv[16], mvp[16];
        arcball_matrix(&ball, model);
        mat_mul(mv, view, model);
        mat_mul(mvp, proj, mv);
        if (flipped)   // a half turn in the screen plane: negate clip x and y
            for (int i = 0; i < 4; i++) { mvp[i * 4] = -mvp[i * 4]; mvp[i * 4 + 1] = -mvp[i * 4 + 1]; }

        glClearColor(0.07f, 0.08f, 0.13f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
        glUniformMatrix4fv(u_model, 1, GL_FALSE, model);
        glUniformMatrix4fv(u_modelview, 1, GL_FALSE, mv);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        control_overlay_draw(overlay, &controls.panel, &controls.status, flipped, now_ms,
                             controls.read_at_ms != previous_read_at_ms);

        eglSwapBuffers(dpy, egl_surf);
        if (finish) glFinish();

        struct gbm_bo *bo = gbm_surface_lock_front_buffer(surf);
        if (!bo) { fprintf(stderr, "lock_front_buffer failed\n"); return 1; }
        uint32_t fb = fb_for(fd, bo);

        if (first) {
            if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) {
                perror("setcrtc"); return 1;
            }
            if (waking) {
                control_runtime_wake(&controls, now_ms);
                if (display_power_on() < 0) fprintf(stderr, "wake: power write failed\n");
                waking = 0;
            } else {
                printf("KMS up: %dx%d@%d on connector %u\n",
                       W, H, mode.vrefresh, conn->connector_id);
                fflush(stdout);
            }
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
            unsigned uploads = control_overlay_take_uploads(overlay);
            if (trace) {
                int n = 0;
                char which[64] = "";
                for (int i = 0; i < TOUCH_MAX_SLOTS; i++)
                    if (input.cube.slot[i].active) {
                        n++;
                        snprintf(which + strlen(which), sizeof which - strlen(which),
                                 "%d(%.0f,%.0f) ", i, input.cube.slot[i].x, input.cube.slot[i].y);
                    }
                printf("%.1f FPS | max frame %.2f ms | accel read %ld us | "
                       "slots %d: %s| cam %.2f pinch_ref %.0f dragging %d | uploads/s %.1f | glGetError 0x%x | "
                       "panel %d slide %.2f armed %d brightness %d auto %d timer %d\n",
                       frames / el, max_frame_ms, accel_max_read_us, n, which,
                       cam, pinch_ref, ball.dragging, uploads / el, glGetError(),
                       controls.panel.open, controls.panel.slide, controls.panel.armed,
                       controls.panel.brightness, controls.panel.auto_on, controls.panel.sleep_minutes);
            } else {
                printf("%.1f FPS\n", frames / el);
            }
            fflush(stdout);
            frames = 0;
            max_frame_ms = 0.0;
            t0 = now;
        }
    }
    return 0;
}
