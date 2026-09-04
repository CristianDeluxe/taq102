// Sweep the LVDS PHY's sampling phase against the panel, with my
// eyes as the meter.
//
// The parallel pixel data leaves the VOP on dclk (CPLL/8 = 50 MHz) and is
// serialised by the PHY on its own PLL (24 MHz / 12 * 175 = 350 MHz). The
// ratio is exactly 7 and both PLLs sit on the same crystal, so the two
// clocks never drift apart -- but their phase relationship is arbitrary,
// and when it lands near the edge of the sampling window the ordinary
// jitter of two independent PLLs pushes a sample over the edge now and
// then. That is what a shimmer with occasional coloured flashes looks
// like, and why it is invisible on flat white: with every data bit at one,
// a mis-sampled bit is still one.
//
// The PHY exposes the two knobs that move the sampling point:
// SAMPLE_CLOCK_DIRECTION (which edge) and SAMPLE_CLOCK_PHASE (eight steps
// within it). This walks all sixteen states, six seconds each, and shows
// which one is live as a row of large blocks -- large because a corrupt
// link still renders them legibly, where a caption in 3x5 text would
// itself be shredded. Top row means forward, bottom row means reverse; the
// number of blocks is the phase plus one. The rest of the screen carries
// single-pixel vertical lines, the pattern that toggles every data lane on
// every pixel clock and so fails first.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define PHY_BASE     0x20038000u
#define ANALOG_REG05 0x14         // sample clock phase [6:4], clock lane skew [2:0]
#define ANALOG_REG08 0x20         // sample clock direction, bit 4
#define LVDS_REG01   0x384        // digital enable, bit 7

static volatile sig_atomic_t stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

struct phy { volatile uint32_t *base; };

static uint32_t phy_read(struct phy *p, unsigned off) { return p->base[off / 4]; }
static void phy_write(struct phy *p, unsigned off, uint32_t v) { p->base[off / 4] = v; }

// Several PHY fields are sampled only as the LVDS digital block leaves
// reset, so a write made while it runs changes nothing. Bracket every
// change the way the stock encoder does.
static void phy_apply(struct phy *p, int direction, int phase) {
    uint32_t r1 = phy_read(p, LVDS_REG01);
    phy_write(p, LVDS_REG01, r1 & ~0x80u);
    uint32_t r8 = phy_read(p, ANALOG_REG08);
    phy_write(p, ANALOG_REG08, direction ? (r8 | 0x10u) : (r8 & ~0x10u));
    uint32_t r5 = phy_read(p, ANALOG_REG05);
    phy_write(p, ANALOG_REG05, (r5 & ~0x70u) | ((uint32_t)(phase & 7) << 4));
    usleep(1000);
    phy_write(p, LVDS_REG01, r1 | 0x80u);
}

static void paint(uint32_t *px, int w, int h, int pitch_px, int direction, int phase) {
    for (int y = 0; y < h; y++) {
        uint32_t *row = px + (size_t)y * pitch_px;
        for (int x = 0; x < w; x++) row[x] = (x & 1) ? 0xFF000000 : 0xFFFFFFFF;
    }
    // The blocks: 40 px squares with 20 px gaps, on a black band so the
    // count is readable against the lines.
    int side = 40, gap = 20, n = phase + 1;
    int band_h = side + 2 * gap;
    int y0 = direction ? h - band_h : 0;
    for (int y = y0; y < y0 + band_h; y++) {
        uint32_t *row = px + (size_t)y * pitch_px;
        for (int x = 0; x < w; x++) row[x] = 0xFF000000;
    }
    for (int i = 0; i < n; i++) {
        int x0 = gap + i * (side + gap);
        for (int y = y0 + gap; y < y0 + gap + side; y++) {
            uint32_t *row = px + (size_t)y * pitch_px;
            for (int x = x0; x < x0 + side && x < w; x++) row[x] = 0xFFFFFFFF;
        }
    }
}

int main(int argc, char **argv) {
    int hold = argc > 1 ? atoi(argv[1]) : 6;

    int mem = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem < 0) { perror("open /dev/mem"); return 1; }
    void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mem, PHY_BASE);
    if (map == MAP_FAILED) { perror("mmap phy"); return 1; }
    struct phy phy = { map };

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
    if (px == MAP_FAILED) { perror("mmap fb"); return 1; }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    uint32_t r5 = phy_read(&phy, ANALOG_REG05), r8 = phy_read(&phy, ANALOG_REG08);
    printf("phytune: entry reg05=0x%02x reg08=0x%02x (direction %s, phase %u)\n",
           r5, r8, (r8 & 0x10) ? "reverse" : "forward", (r5 >> 4) & 7);
    fflush(stdout);

    paint(px, W, H, cd.pitch / 4, 1, 0);
    if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) { perror("setcrtc"); return 1; }

    for (int round = 0; !stop; round++) {
        for (int dir = 1; dir >= 0 && !stop; dir--)
            for (int phase = 0; phase < 8 && !stop; phase++) {
                paint(px, W, H, cd.pitch / 4, dir, phase);
                phy_apply(&phy, dir, phase);
                printf("%s phase %d  (blocks %d, %s row)\n",
                       dir ? "reverse" : "forward", phase, phase + 1, dir ? "bottom" : "top");
                fflush(stdout);
                for (int s = 0; s < hold && !stop; s++) sleep(1);
            }
    }
    // Leave the PHY as the driver set it up.
    phy_apply(&phy, 1, (r5 >> 4) & 7);
    return 0;
}
