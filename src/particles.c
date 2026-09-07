// Particle field on the TAQ-102 panel, drawn straight to a DRM dumb buffer.
// Touch attracts. No X, no Wayland, no compositor -- one process owning KMS.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <linux/input-event-codes.h>

// The kernel's input_event on a 32-bit kernel is 16 bytes. musl with 64-bit
// time_t makes struct timeval 16 bytes, so its struct input_event is 24 and a
// `read() == sizeof(ev)` check silently never matches. Pin the wire format.
struct kev { uint32_t sec, usec; uint16_t type, code; int32_t value; };
#include <xf86drm.h>
#include <xf86drmMode.h>

#define NP 6000

struct p { float x, y, vx, vy; unsigned char r, g, b; };

static float frand(void) { return (float)rand() / (float)RAND_MAX; }


// 3x5 glyphs: digits, then F P S and '.'
static const unsigned char GLYPH[14][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
    {7,4,7,4,4},{7,5,7,4,4},{7,4,7,1,7},{0,0,0,0,2}
};

static void glyph(uint32_t *buf, int W, int H, int gx, int gy, int g, int s, uint32_t col) {
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (GLYPH[g][r] & (4 >> c))
                for (int dy = 0; dy < s; dy++)
                    for (int dx = 0; dx < s; dx++) {
                        int x = gx + c * s + dx, y = gy + r * s + dy;
                        if (x >= 0 && x < W && y >= 0 && y < H) buf[y * W + x] = col;
                    }
}

static void text(uint32_t *buf, int W, int H, int x, int y, const char *str, int s, uint32_t col) {
    for (const char *p = str; *p; p++) {
        int g = -1;
        if (*p >= '0' && *p <= '9') g = *p - '0';
        else if (*p == 'F') g = 10;
        else if (*p == 'P') g = 11;
        else if (*p == 'S') g = 12;
        else if (*p == '.') g = 13;
        if (g >= 0) glyph(buf, W, H, x, y, g, s, col);
        x += 4 * s;
    }
}


// Four Cortex-A7 cores and no usable GPU: the fade and the blit are the two
// bandwidth-bound passes, and both split cleanly by row band.
#define NTHREADS 4

static struct {
    uint32_t *shadow; uint8_t *base; uint32_t pitch;
    int W, H, phase;
    pthread_barrier_t start, done;
} job;

static void fade_band(int y0, int y1) {
    for (int i = y0 * job.W, n = y1 * job.W; i < n; i++) {
        uint32_t c = job.shadow[i];
        job.shadow[i] = c - ((c >> 3) & 0x1f1f1f);
    }
}

static void blit_band(int y0, int y1) {
    for (int y = y0; y < y1; y++)
        memcpy(job.base + (size_t)y * job.pitch,
               job.shadow + (size_t)y * job.W, (size_t)job.W * 4);
}

static void on_flip(int fd, unsigned seq, unsigned s, unsigned us, void *data) {
    (void)fd; (void)seq; (void)s; (void)us;
    *(int *)data = 1;
}

static void *worker(void *arg) {
    long id = (long)arg;
    int band = job.H / NTHREADS;
    int y0 = id * band, y1 = (id == NTHREADS - 1) ? job.H : y0 + band;
    for (;;) {
        pthread_barrier_wait(&job.start);
        if (job.phase == 0) fade_band(y0, y1); else blit_band(y0, y1);
        pthread_barrier_wait(&job.done);
    }
    return NULL;
}

static void run_phase(int phase) {
    job.phase = phase;
    pthread_barrier_wait(&job.start);
    pthread_barrier_wait(&job.done);
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

    // Two scanout buffers, so a finished frame is shown by a page flip at
    // vblank instead of being torn into the buffer the CRTC is reading.
    struct drm_mode_create_dumb creq = { .width = W, .height = H, .bpp = 32 };
    uint32_t fb[2]; uint8_t *bufs[2];
    for (int i = 0; i < 2; i++) {
        struct drm_mode_create_dumb c = { .width = W, .height = H, .bpp = 32 };
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0) { perror("create dumb"); return 1; }
        if (drmModeAddFB(fd, W, H, 24, 32, c.pitch, c.handle, &fb[i])) { perror("addfb"); return 1; }
        struct drm_mode_map_dumb m = { .handle = c.handle };
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) { perror("map dumb"); return 1; }
        bufs[i] = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
        if (bufs[i] == MAP_FAILED) { perror("mmap"); return 1; }
        // The stock VOP blends XRGB as ARGB, including the initial black.
        for (size_t pixel = 0; pixel < c.size / sizeof(uint32_t); pixel++)
            ((uint32_t *)bufs[i])[pixel] = 0xFF000000u;
        if (i == 0) creq = c;
    }
    uint8_t *base = bufs[0];

    // Render into cached RAM, never read back from the framebuffer: a DRM dumb
    // buffer maps write-combining, so writes are tolerable but reads are not.
    uint32_t *shadow = calloc((size_t)W * H, 4);
    if (!shadow) { perror("shadow"); return 1; }
    for (size_t pixel = 0; pixel < (size_t)W * H; pixel++)
        shadow[pixel] = 0xFF000000u;

    job.shadow = shadow; job.base = base; job.pitch = creq.pitch;
    job.W = W; job.H = H;
    pthread_barrier_init(&job.start, NULL, NTHREADS + 1);
    pthread_barrier_init(&job.done, NULL, NTHREADS + 1);
    for (long i = 0; i < NTHREADS; i++) {
        pthread_t th; pthread_create(&th, NULL, worker, (void *)i); pthread_detach(th);
    }
    printf("workers: %d\n", NTHREADS); fflush(stdout);

    if (drmModeSetCrtc(fd, crtc_id, fb[0], 0, 0, &conn->connector_id, 1, &mode)) {
        perror("setcrtc"); return 1;
    }
    printf("KMS up: %dx%d@%d on connector %u\n", W, H, mode.vrefresh, conn->connector_id);

    int tfd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    float tx = W / 2.f, ty = H / 2.f; int touching = 0;

    struct p *ps = malloc(sizeof(struct p) * NP);
    for (int i = 0; i < NP; i++) {
        ps[i].x = frand() * W; ps[i].y = frand() * H;
        ps[i].vx = (frand() - .5f) * 2.f; ps[i].vy = (frand() - .5f) * 2.f;
        int h = rand() % 3;
        ps[i].r = h == 0 ? 255 : 120 + rand() % 136;
        ps[i].g = h == 1 ? 255 : 140 + rand() % 116;
        ps[i].b = h == 2 ? 255 : 180 + rand() % 76;
    }

    uint32_t pitch = creq.pitch;
    for (long frame = 0;; frame++) {
        struct kev ev;
        while (tfd >= 0 && read(tfd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_MT_POSITION_X) { tx = ev.value; touching = 1; }
                else if (ev.code == ABS_MT_POSITION_Y) { ty = ev.value; touching = 1; }
                else if (ev.code == ABS_MT_TRACKING_ID && ev.value == -1) touching = 0;
            }
        }

        run_phase(0);  // fade, across all cores

        for (int i = 0; i < NP; i++) {
            struct p *q = &ps[i];
            float dx = tx - q->x, dy = ty - q->y;
            float d2 = dx * dx + dy * dy + 400.f;
            float f = (touching ? 900.f : 260.f) / d2;
            q->vx += dx * f; q->vy += dy * f;
            q->vx *= 0.985f; q->vy *= 0.985f;
            q->x += q->vx; q->y += q->vy;
            if (q->x < 0) { q->x = 0; q->vx = -q->vx * .8f; }
            if (q->x >= W) { q->x = W - 1; q->vx = -q->vx * .8f; }
            if (q->y < 0) { q->y = 0; q->vy = -q->vy * .8f; }
            if (q->y >= H) { q->y = H - 1; q->vy = -q->vy * .8f; }
            int px = (int)q->x, py = (int)q->y;
            uint32_t core = 0xFF000000u | (q->r << 16) | (q->g << 8) | q->b;
            uint32_t halo = 0xFF000000u | ((q->r >> 1) << 16) | ((q->g >> 1) << 8) | (q->b >> 1);
            for (int dy = -1; dy <= 1; dy++) {
                int yy = py + dy;
                if (yy < 0 || yy >= H) continue;
                uint32_t *row = shadow + (size_t)yy * W;
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = px + dx;
                    if (xx < 0 || xx >= W) continue;
                    row[xx] = (dx == 0 && dy == 0) ? core : halo;
                }
            }
        }
        // frame counter, refreshed once a second
        static struct timespec t0; static long frames; static char fps[16] = "0.0 FPS";
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (t0.tv_sec == 0) t0 = now;
        frames++;
        double el = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
        if (el >= 1.0) {
            double f = frames / el;
            snprintf(fps, sizeof fps, "%d.%d FPS", (int)f, (int)(f * 10) % 10);
            printf("%s\n", fps); fflush(stdout);
            frames = 0; t0 = now;
        }
        text(shadow, W, H, 12, 12, fps, 4, 0xFFFFFFFFu);

        int back = frame & 1;
        job.base = bufs[back];
        run_phase(1);  // blit, across all cores

        // hand the finished buffer to the CRTC and wait for vblank
        int flip_done = 0;
        if (drmModePageFlip(fd, crtc_id, fb[back], DRM_MODE_PAGE_FLIP_EVENT, &flip_done) == 0) {
            drmEventContext evctx = { .version = 2, .page_flip_handler = on_flip };
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            while (!flip_done && poll(&pfd, 1, 100) > 0) drmHandleEvent(fd, &evctx);
        }
    }
    return 0;
}
