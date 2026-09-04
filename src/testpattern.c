// Static test patterns on the panel, straight to a DRM dumb buffer, to tell
// apart what can make a still picture shimmer. One buffer, one modeset, no
// page flips and no GPU: whatever moves on screen after this is the display
// path, not the renderer.
//
//   testpattern [pattern]     vlines hlines checker grey white black bars
//
// The patterns are chosen to load the LVDS link differently. Single-pixel
// vertical lines flip the data lanes on every pixel clock, which is the
// worst case for the serializer and for a marginal termination; horizontal
// lines flip them once per line; a flat grey field holds the lanes almost
// still and leaves only the backlight and the panel's own supplies. A defect
// that shows on vlines and not on grey is on the link; one that shows on all
// of them is not.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static volatile sig_atomic_t stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

static void paint(uint32_t *px, int w, int h, int pitch_px, const char *pat) {
    for (int y = 0; y < h; y++) {
        uint32_t *row = px + (size_t)y * pitch_px;
        for (int x = 0; x < w; x++) {
            uint32_t c;
            if (!strcmp(pat, "vlines"))       c = (x & 1) ? 0xFF000000 : 0xFFFFFFFF;
            else if (!strcmp(pat, "hlines"))  c = (y & 1) ? 0xFF000000 : 0xFFFFFFFF;
            else if (!strcmp(pat, "checker")) c = ((x ^ y) & 1) ? 0xFF000000 : 0xFFFFFFFF;
            else if (!strcmp(pat, "grey"))    c = 0xFF808080;
            else if (!strcmp(pat, "white"))   c = 0xFFFFFFFF;
            else if (!strcmp(pat, "black"))   c = 0xFF000000;
            else {   // bars: the four cases side by side, 256 px each
                int band = x / 256;
                c = band == 0 ? ((x & 1) ? 0xFF000000 : 0xFFFFFFFF)
                  : band == 1 ? ((y & 1) ? 0xFF000000 : 0xFFFFFFFF)
                  : band == 2 ? 0xFF808080
                              : 0xFFFFFFFF;
            }
            row[x] = c;
        }
    }
}

int main(int argc, char **argv) {
    const char *pat = argc > 1 ? argv[1] : "bars";

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

    struct drm_mode_create_dumb cd = { .width = W, .height = H, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) { perror("create dumb"); return 1; }
    uint32_t fb;
    if (drmModeAddFB(fd, W, H, 24, 32, cd.pitch, cd.handle, &fb)) { perror("addfb"); return 1; }
    struct drm_mode_map_dumb m = { .handle = cd.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) { perror("map dumb"); return 1; }
    uint32_t *px = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    if (px == MAP_FAILED) { perror("mmap"); return 1; }

    paint(px, W, H, cd.pitch / 4, pat);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    // Set the mode twice, as rescue-screen does: the first modeset after a
    // handover can leave the panel blank.
    if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) { perror("setcrtc"); return 1; }
    usleep(200000);
    drmModeSetCrtc(fd, crtc_id, 0, 0, 0, NULL, 0, NULL);
    usleep(100000);
    if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) { perror("setcrtc again"); return 1; }

    printf("testpattern: %s on %dx%d, one buffer, no flips\n", pat, W, H);
    fflush(stdout);
    while (!stop) sleep(1);
    return 0;
}
