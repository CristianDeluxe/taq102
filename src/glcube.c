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
#include <linux/input-event-codes.h>

// The kernel's input_event on this 32-bit kernel is 16 bytes; a libc with
// 64-bit time_t describes it as 24 and a sizeof() check then never matches.
// Pin the wire format, as particles.c does.
struct kev { uint32_t sec, usec; uint16_t type, code; int32_t value; };

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static const char *VERT =
    "attribute vec3 pos;\n"
    "attribute vec3 nrm;\n"
    "attribute vec3 col;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 model;\n"
    "varying vec3 v_col;\n"
    "varying vec3 v_nrm;\n"
    "void main() {\n"
    "  v_col = col;\n"
    "  v_nrm = (model * vec4(nrm, 0.0)).xyz;\n"
    "  gl_Position = mvp * vec4(pos, 1.0);\n"
    "}\n";

static const char *FRAG =
    "precision mediump float;\n"
    "varying vec3 v_col;\n"
    "varying vec3 v_nrm;\n"
    "void main() {\n"
    "  vec3 l = normalize(vec3(0.4, 0.7, 1.0));\n"
    "  float d = max(dot(normalize(v_nrm), l), 0.0);\n"
    "  gl_FragColor = vec4(v_col * (0.25 + 0.75 * d), 1.0);\n"
    "}\n";

// Six faces, two triangles each: position, normal, colour per vertex.
#define F(nx, ny, nz, r, g, b, ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz) \
    ax, ay, az, nx, ny, nz, r, g, b,  bx, by, bz, nx, ny, nz, r, g, b, \
    cx, cy, cz, nx, ny, nz, r, g, b,  ax, ay, az, nx, ny, nz, r, g, b, \
    cx, cy, cz, nx, ny, nz, r, g, b,  dx, dy, dz, nx, ny, nz, r, g, b

static const GLfloat CUBE[] = {
    F( 0, 0, 1,  0.95f, 0.30f, 0.35f,  -1,-1, 1,   1,-1, 1,   1, 1, 1,  -1, 1, 1),
    F( 0, 0,-1,  0.30f, 0.70f, 0.95f,   1,-1,-1,  -1,-1,-1,  -1, 1,-1,   1, 1,-1),
    F( 1, 0, 0,  0.98f, 0.75f, 0.25f,   1,-1, 1,   1,-1,-1,   1, 1,-1,   1, 1, 1),
    F(-1, 0, 0,  0.45f, 0.90f, 0.55f,  -1,-1,-1,  -1,-1, 1,  -1, 1, 1,  -1, 1,-1),
    F( 0, 1, 0,  0.85f, 0.85f, 0.90f,  -1, 1, 1,   1, 1, 1,   1, 1,-1,  -1, 1,-1),
    F( 0,-1, 0,  0.60f, 0.45f, 0.95f,  -1,-1,-1,   1,-1,-1,   1,-1, 1,  -1,-1, 1),
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

static void mat_rotate(float *m, float ax, float ay) {
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay);
    float rx[16], ry[16];
    mat_identity(rx); rx[5] = cx; rx[6] = sx; rx[9] = -sx; rx[10] = cx;
    mat_identity(ry); ry[0] = cy; ry[2] = -sy; ry[8] = sy; ry[10] = cy;
    mat_mul(m, ry, rx);
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
    view[14] = -6.f;

    int tfd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    float ax = 0.4f, ay = 0.6f, vx = 0.006f, vy = 0.011f;
    float last_tx = 0, last_ty = 0;
    int touching = 0;

    struct gbm_bo *prev_bo = NULL;
    int first = 1;
    struct timespec t0 = {0, 0};
    long frames = 0;

    for (;;) {
        struct kev ev;
        while (tfd >= 0 && read(tfd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
            if (ev.type != EV_ABS) continue;
            if (ev.code == ABS_MT_POSITION_X) {
                if (touching) vy = (ev.value - last_tx) * 0.0015f;
                last_tx = ev.value; touching = 1;
            } else if (ev.code == ABS_MT_POSITION_Y) {
                if (touching) vx = (ev.value - last_ty) * 0.0015f;
                last_ty = ev.value; touching = 1;
            } else if (ev.code == ABS_MT_TRACKING_ID && ev.value == -1) {
                touching = 0;
            }
        }

        ax += vx; ay += vy;

        float model[16], mv[16], mvp[16];
        mat_rotate(model, ax, ay);
        mat_mul(mv, view, model);
        mat_mul(mvp, proj, mv);

        glClearColor(0.04f, 0.05f, 0.08f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
        glUniformMatrix4fv(u_model, 1, GL_FALSE, model);
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
            printf("%.1f FPS\n", frames / el);
            fflush(stdout);
            frames = 0; t0 = now;
        }
    }
    return 0;
}
