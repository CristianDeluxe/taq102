// Particle field on the TAQ-102 panel, drawn straight to a DRM dumb buffer.
// Touch attracts. No X, no Wayland, no compositor -- one process owning KMS.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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

#define NP 900

struct p { float x, y, vx, vy; unsigned char r, g, b; };

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

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

    struct drm_mode_create_dumb creq = { .width = W, .height = H, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) { perror("create dumb"); return 1; }

    uint32_t fb;
    if (drmModeAddFB(fd, W, H, 24, 32, creq.pitch, creq.handle, &fb)) { perror("addfb"); return 1; }

    struct drm_mode_map_dumb mreq = { .handle = creq.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) { perror("map dumb"); return 1; }

    uint8_t *base = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }
    memset(base, 0, creq.size);

    if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) {
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
        ps[i].r = h == 0 ? 255 : 40 + rand() % 80;
        ps[i].g = h == 1 ? 255 : 60 + rand() % 90;
        ps[i].b = h == 2 ? 255 : 120 + rand() % 135;
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

        // fade instead of clear, so the particles leave trails
        for (int y = 0; y < H; y++) {
            uint32_t *row = (uint32_t *)(base + y * pitch);
            for (int x = 0; x < W; x++) {
                uint32_t c = row[x];
                row[x] = ((c >> 1) & 0x7f7f7f);
            }
        }

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
            uint32_t *row = (uint32_t *)(base + py * pitch);
            row[px] = (q->r << 16) | (q->g << 8) | q->b;
            if (px + 1 < W) row[px + 1] = (q->r << 16) | (q->g << 8) | q->b;
        }
        usleep(16000);
    }
    return 0;
}
