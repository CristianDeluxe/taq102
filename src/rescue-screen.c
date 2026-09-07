// The rescue image's face: an amber screen that says RESCUE MODE, with the
// kernel, the build id, the Wi-Fi address and signal and the battery, drawn
// straight to a DRM dumb buffer. It exists because a rescue that does not
// draw looks exactly like an appliance that failed -- under the stock kernel
// nothing sets a mode until an application does, and the panel just shows
// backlight.
//
// Top right, the iOS-style status bar from statusbar.c, which glcube shares.
//
// One buffer, no page flips: status and orientation are sampled every two
// seconds, with a repaint when either changes. Like glcube, three Y samples
// above +500 mg turn the entire canvas halfway round; below -500 mg turn it
// back, keeping the orientation between thresholds. No sensor means normal.
// RESCUE_FLIP=0|1 forces orientation without a sensor or physically turning
// the tablet. Runs until killed; `killall rescue-screen` frees the display.
// RESCUE_DUMP=<file.ppm> writes the oriented frame and refuses a symlink at
// that path, so the screen can be checked without a camera.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "accel.h"
#include "canvas.h"
#include "status.h"
#include "statusbar.h"

// Alpha 0xFF on purpose. The framebuffer is added as XRGB8888, but the stock
// 4.4.103 VOP driver blends it as ARGB: with 0x00 in the top byte the whole
// window is transparent and the panel shows a washed-out white with a ghost
// of the picture. Measured 2026-09-03; glcube never hit it because GBM
// buffers carry 0xFF. The own 4.4.167 kernel does not care either way.
#define AMBER 0xFFE08A00u
#define INK   0xFF201000u
#define PALE  0xFFFFF3D0u
#define DIM   0xFFB8741Cu   // unlit arcs and the status bar: amber, darkened
#define GREEN 0xFF30C048u   // iOS charging green
#define RED   0xFFE03030u   // iOS low-battery red

static void read_line(const char *path, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0;
    fclose(f);
}

static const struct statusbar_style BAR = { DIM, AMBER, INK, DIM, PALE };

static void paint(struct canvas *c, const char *kernel, const char *build, const struct status *st) {
    for (int i = 0; i < c->w * c->h; i++) c->px[i] = AMBER;
    statusbar_paint(c, st, &BAR);
    int big = c->w / 60;                    // "RESCUE MODE" is 11 glyphs, 4 units each
    canvas_text(c, (c->w - 11 * 4 * big + big) / 2, c->h / 5, "RESCUE MODE", big, INK);
    int s = c->w / 200;
    int y = c->h / 2, dy = 7 * s, x = c->w / 12;
    char line[96];
    snprintf(line, sizeof line, "KERNEL %s", kernel); canvas_text(c, x, y, line, s, INK); y += dy;
    snprintf(line, sizeof line, "BUILD %s", build);   canvas_text(c, x, y, line, s, INK); y += dy;
    if (st->have_wifi) snprintf(line, sizeof line, "WIFI %s %dDBM Q%d", st->addr, st->level, st->quality);
    else snprintf(line, sizeof line, "WIFI %s", st->addr);
    canvas_text(c, x, y, line, s, PALE); y += dy;
    if (st->have_batt) snprintf(line, sizeof line, "BATTERY %d%% %d.%02dV %s %dMA", st->cap, st->mv / 1000, (st->mv / 10) % 100, st->word, st->ma);
    else snprintf(line, sizeof line, "BATTERY UNKNOWN");
    canvas_text(c, x, y, line, s, PALE); y += dy;
    canvas_text(c, x, y, "SSH ROOT - TTYGS0 - KILLALL RESCUE-SCREEN TO DRAW", s, INK);
}

static void dump_ppm(const struct canvas *c, const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) { perror("RESCUE_DUMP open"); return; }
    FILE *f = fdopen(fd, "wb");
    if (!f) { perror("RESCUE_DUMP fdopen"); close(fd); return; }
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        unsigned char rgb[3] = { (p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static volatile sig_atomic_t stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

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

    struct drm_mode_create_dumb cd = { .width = W, .height = H, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) { perror("create dumb"); return 1; }
    uint32_t fb;
    if (drmModeAddFB(fd, W, H, 24, 32, cd.pitch, cd.handle, &fb)) { perror("addfb"); return 1; }
    struct drm_mode_map_dumb m = { .handle = cd.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) { perror("map dumb"); return 1; }
    uint8_t *base = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }

    // Render into cached RAM and copy out: the dumb buffer maps write-combining.
    struct canvas canvas = { calloc((size_t)W * H, 4), W, H };
    if (!canvas.px) { perror("shadow"); return 1; }

    struct utsname u; uname(&u);
    char build[64], shown[200] = "", key[200];
    read_line("/etc/taq102-build-id", build, sizeof build);
    const char *dump = getenv("RESCUE_DUMP");
    const char *override = getenv("RESCUE_FLIP");
    int forced = override && (!strcmp(override, "0") || !strcmp(override, "1"));
    if (override && !forced)
        fprintf(stderr, "invalid RESCUE_FLIP=%s; using accelerometer\n", override);
    int flipped = forced && !strcmp(override, "1"), flip_votes = 0;
    int accel_fd = forced ? -1 : accel_open();
    printf("accelerometer: %s; orientation: %s\n",
           forced ? "override" : accel_fd >= 0 ? "enabled" : "unavailable",
           flipped ? "turned round" : "normal");

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    int first = 1;
    while (!stop) {
        int orientation_changed = 0;
        if (accel_fd >= 0) {
            int x_mg, y_mg, z_mg;
            if (accel_read(accel_fd, &x_mg, &y_mg, &z_mg) == 0) {
                int want = y_mg > 500 ? 1 : y_mg < -500 ? 0 : flipped;
                if (want != flipped && ++flip_votes >= 3) {
                    flipped = want;
                    flip_votes = 0;
                    orientation_changed = 1;
                    printf("orientation: %s (y %d mg)\n",
                           flipped ? "turned round" : "normal", y_mg);
                } else if (want == flipped) {
                    flip_votes = 0;
                }
            } else {
                flip_votes = 0;
            }
        }
        struct status st;
        status_read(&st);
        snprintf(key, sizeof key, "%s|%d|%d|%d|%d|%d|%s", st.addr, st.have_wifi, st.level, st.quality, st.cap, st.ma > 0, st.word);
        if (first || orientation_changed || strcmp(key, shown)) {
            paint(&canvas, u.release, build, &st);
            if (flipped) {
                size_t count = (size_t)W * H;
                for (size_t i = 0; i < count / 2; i++) {
                    uint32_t pixel = canvas.px[i];
                    canvas.px[i] = canvas.px[count - 1 - i];
                    canvas.px[count - 1 - i] = pixel;
                }
            }
            for (int y = 0; y < H; y++) memcpy(base + (size_t)y * cd.pitch, canvas.px + (size_t)y * W, (size_t)W * 4);
            if (dump) dump_ppm(&canvas, dump);
            if (first) {
                // Under the stock 4.4.103 kernel the first modeset after boot
                // leaves the panel blank and the second shows the picture --
                // measured 2026-09-03: a fresh rescue boot was white, a restart
                // of this program was not. glcube never sees it because it page
                // flips right after. So set the mode, drop the CRTC, set it
                // again; on the 4.4.167 kernel the extra cycle is harmless.
                if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) {
                    perror("setcrtc"); return 1;
                }
                usleep(200000);
                drmModeSetCrtc(fd, crtc_id, 0, 0, 0, NULL, 0, NULL);
                usleep(100000);
                if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) {
                    perror("setcrtc again"); return 1;
                }
            }
            strcpy(shown, key);
            first = 0;
            printf("rescue-screen: %dx%d, %s %ddBm q%d / %d%% %dmA\n", W, H, st.addr, st.level, st.quality, st.cap, st.ma);
            fflush(stdout);
        }
        sleep(2);
    }
    if (accel_fd >= 0) close(accel_fd);
    return 0;
}
