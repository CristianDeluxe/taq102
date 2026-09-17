// Page-flip test: two dumb buffers with identical content, flipped at every
// vblank. If the panel flickers on this, the flip path is the fault; if it
// does not, the flicker comes from what the GPU draws.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <stdint.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static int flip_done;
static void on_flip(int fd, unsigned seq, unsigned s, unsigned us, void *data) {
    (void)fd; (void)seq; (void)s; (void)us; (void)data;
    flip_done = 1;
}

int main(int argc, char **argv) {
    // fliptest [color-hex-a] [color-hex-b] [pause-ms]: default identical
    // amber at every vblank; give two colours to see whether alternating
    // content shows as flicker, and a pause to slow the flips down to a rate
    // "vlines" as the first argument paints single-pixel vertical lines into
    // both buffers instead: a flat field cannot show a displacement, and the
    // camera rig measures displacement on lines.
    // a webcam can resolve (I see the fast one; the camera does not).
    uint32_t col[2] = { 0xFFE08A00u, 0xFFE08A00u };
    int pause_ms = 0;
    int vlines = argc > 1 && !strcmp(argv[1], "vlines");
    if (argc > 1 && !vlines) col[0] = col[1] = (uint32_t)strtoul(argv[1], NULL, 16);
    if (argc > 2) col[1] = (uint32_t)strtoul(argv[2], NULL, 16);
    if (argc > 3) pause_ms = atoi(argv[3]);

    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open card0"); return 1; }
    drmModeRes *res = drmModeGetResources(fd);
    drmModeConnector *conn = NULL;
    for (int i = 0; res && i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) { conn = c; break; }
        if (c) drmModeFreeConnector(c);
    }
    if (!conn) { fprintf(stderr, "no connector\n"); return 1; }
    drmModeModeInfo mode = conn->modes[0];
    int W = mode.hdisplay, H = mode.vdisplay;
    drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
    uint32_t crtc_id = enc ? enc->crtc_id : res->crtcs[0];

    uint32_t fb[2];
    for (int i = 0; i < 2; i++) {
        struct drm_mode_create_dumb c = { .width = W, .height = H, .bpp = 32 };
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0) { perror("create dumb"); return 1; }
        if (drmModeAddFB(fd, W, H, 24, 32, c.pitch, c.handle, &fb[i])) { perror("addfb"); return 1; }
        struct drm_mode_map_dumb m = { .handle = c.handle };
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) { perror("map dumb"); return 1; }
        uint32_t *p = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
        if (p == MAP_FAILED) { perror("mmap"); return 1; }
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                p[y * (c.pitch / 4) + x] = vlines ? ((x & 1) ? 0xFF000000u : 0xFFFFFFFFu)
                                                  : col[i];
        munmap(p, c.size);
    }

    if (drmModeSetCrtc(fd, crtc_id, fb[0], 0, 0, &conn->connector_id, 1, &mode)) { perror("setcrtc"); return 1; }
    usleep(200000);
    drmModeSetCrtc(fd, crtc_id, 0, 0, 0, NULL, 0, NULL);
    usleep(100000);
    if (drmModeSetCrtc(fd, crtc_id, fb[0], 0, 0, &conn->connector_id, 1, &mode)) { perror("setcrtc"); return 1; }

    drmEventContext evctx = { .version = 2, .page_flip_handler = on_flip };
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    unsigned long flips = 0;
    // FLIP_SAME=1 flips to the buffer already on screen every time: the
    // cfg_done cycle without the address change, to tell the two apart.
    int same = getenv("FLIP_SAME") != NULL;
    for (int i = 1;; i ^= 1) {
        flip_done = 0;
        if (drmModePageFlip(fd, crtc_id, fb[same ? 0 : i], DRM_MODE_PAGE_FLIP_EVENT, NULL)) { perror("flip"); return 1; }
        while (!flip_done && poll(&pfd, 1, 100) > 0) drmHandleEvent(fd, &evctx);
        if (pause_ms) usleep((useconds_t)pause_ms * 1000);
        if (++flips % (pause_ms ? 10 : 550) == 0) { printf("fliptest: %lu flips\n", flips); fflush(stdout); }
    }
}
