// The rescue image's face: an amber screen that says RESCUE MODE, with the
// kernel, the build id, the Wi-Fi address and signal and the battery, drawn
// straight to a DRM dumb buffer. It exists because a rescue that does not draw looks exactly like an
// appliance that failed -- under the stock kernel nothing sets a mode until an
// application does, and the panel just shows backlight.
//
// One buffer, no page flips: the picture changes only when a status line
// does, and a full redraw once every two seconds is nothing. Runs until killed;
// `killall rescue-screen` frees the display for whatever is being debugged.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

// Alpha 0xFF on purpose. The framebuffer is added as XRGB8888, but the stock
// 4.4.103 VOP driver blends it as ARGB: with 0x00 in the top byte the whole
// window is transparent and the panel shows a washed-out white with a ghost
// of the picture. Measured 2026-09-03; glcube never hit it because GBM
// buffers carry 0xFF. The own 4.4.167 kernel does not care either way.
#define AMBER 0xFFE08A00u
#define INK   0xFF201000u
#define PALE  0xFFFFF3D0u

// 3x5 glyphs, three bits per row, bit 2 the left column: A..Z, 0..9, then
// '.', ':', '-', '/', '%'. Anything else advances without drawing.
static const unsigned char GLYPH[41][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,7,4,7},{7,4,7,4,4},
    {7,4,5,5,7},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,7},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{6,5,5,5,5},{7,5,5,5,7},{7,5,7,4,4},{7,5,5,7,1},{7,5,6,5,5},
    {7,4,7,1,7},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
    {0,0,0,0,2},{0,2,0,2,0},{0,0,7,0,0},{1,1,2,4,4},{5,1,2,4,5}
};

static int glyph_index(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
    switch (ch) { case '.': return 36; case ':': return 37; case '-': return 38; case '/': return 39; case '%': return 40; }
    return -1;
}

static void text(uint32_t *buf, int W, int H, int x, int y, const char *str, int s, uint32_t col) {
    for (const char *p = str; *p; p++, x += 4 * s) {
        int g = glyph_index(*p);
        if (g < 0) continue;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if (GLYPH[g][r] & (4 >> c))
                    for (int dy = 0; dy < s; dy++)
                        for (int dx = 0; dx < s; dx++) {
                            int px = x + c * s + dx, py = y + r * s + dy;
                            if (px >= 0 && px < W && py >= 0 && py < H) buf[py * W + px] = col;
                        }
    }
}

static void read_line(const char *path, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0;
    fclose(f);
}

static void wlan_address(char *out, size_t n) {
    struct ifreq ifr = { 0 };
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
    if (s >= 0 && ioctl(s, SIOCGIFADDR, &ifr) == 0)
        snprintf(out, n, "%s", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    else
        snprintf(out, n, "NO WIFI YET");
    if (s >= 0) close(s);
}

// "BATTERY 6% 3.52V CHARGING -320MA": the rk816 driver's view, which is
// what decides whether the tablet is about to switch off. current_now is
// negative while discharging, and it stayed negative on a Mac USB port with
// the status still saying Charging -- the number, not the word, is the truth.
static void battery_line(char *out, size_t n) {
    char cap[16], vol[16], cur[16], st[16];
    read_line("/sys/class/power_supply/battery/capacity", cap, sizeof cap);
    read_line("/sys/class/power_supply/battery/voltage_now", vol, sizeof vol);
    read_line("/sys/class/power_supply/battery/current_now", cur, sizeof cur);
    read_line("/sys/class/power_supply/battery/status", st, sizeof st);
    if (!cap[0]) { snprintf(out, n, "BATTERY UNKNOWN"); return; }
    snprintf(out, n, "BATTERY %s%% %d.%02dV %s %dMA", cap,
             atoi(vol) / 1000000, (atoi(vol) / 10000) % 100, st, atoi(cur) / 1000);
}

// "WIFI 192.168.1.57 -37DBM Q100": address from the interface, level and
// link quality from /proc/net/wireless, which is what the driver reports for
// the association it holds. Both go NO WIFI YET until the driver is up.
static void wifi_line(char *out, size_t n) {
    char addr[64];
    wlan_address(addr, sizeof addr);
    int quality = -1, level = 0;
    FILE *f = fopen("/proc/net/wireless", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char *w = strstr(line, "wlan0:");
            if (!w) continue;
            // "0000  100.  -37.  -256." -- %d then a literal dot; %f would
            // swallow "100." whole and the match would fail on the dot.
            int q = 0, l = 0;
            if (sscanf(w + 6, " %*x %d. %d.", &q, &l) == 2) { quality = q; level = l; }
        }
        fclose(f);
    }
    if (quality < 0) snprintf(out, n, "WIFI %s", addr);
    else snprintf(out, n, "WIFI %s %dDBM Q%d", addr, level, quality);
}

static volatile sig_atomic_t stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

static void paint(uint32_t *shadow, int W, int H, const char *kernel, const char *build, const char *status) {
    for (int i = 0; i < W * H; i++) shadow[i] = AMBER;
    int big = W / 60;                       // "RESCUE MODE" is 11 glyphs, 4 units each
    text(shadow, W, H, (W - 11 * 4 * big + big) / 2, H / 5, "RESCUE MODE", big, INK);
    int s = W / 200;
    int y = H / 2, dy = 7 * s;
    char line[96];
    snprintf(line, sizeof line, "KERNEL %s", kernel); text(shadow, W, H, W / 12, y, line, s, INK); y += dy;
    snprintf(line, sizeof line, "BUILD %s", build);   text(shadow, W, H, W / 12, y, line, s, INK); y += dy;
    // status holds the Wi-Fi and battery lines separated by a newline
    const char *nl = strchr(status, '\n');
    snprintf(line, sizeof line, "%.*s", (int)(nl ? nl - status : (long)strlen(status)), status);
    text(shadow, W, H, W / 12, y, line, s, PALE); y += dy;
    if (nl) { text(shadow, W, H, W / 12, y, nl + 1, s, PALE); y += dy; }
    text(shadow, W, H, W / 12, y, "SSH ROOT - USB CONSOLE TTYGS0 - KILLALL RESCUE-SCREEN TO DRAW", s, INK);
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

    struct drm_mode_create_dumb c = { .width = W, .height = H, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0) { perror("create dumb"); return 1; }
    uint32_t fb;
    if (drmModeAddFB(fd, W, H, 24, 32, c.pitch, c.handle, &fb)) { perror("addfb"); return 1; }
    struct drm_mode_map_dumb m = { .handle = c.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) { perror("map dumb"); return 1; }
    uint8_t *base = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }

    // Render into cached RAM and copy out: the dumb buffer maps write-combining.
    uint32_t *shadow = calloc((size_t)W * H, 4);
    if (!shadow) { perror("shadow"); return 1; }

    struct utsname u; uname(&u);
    char build[64], wifi[96], batt[96], status[200], shown[200] = "";
    read_line("/etc/taq102-build-id", build, sizeof build);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    int first = 1;
    while (!stop) {
        wifi_line(wifi, sizeof wifi);
        battery_line(batt, sizeof batt);
        snprintf(status, sizeof status, "%s\n%s", wifi, batt);
        if (first || strcmp(status, shown)) {
            paint(shadow, W, H, u.release, build, status);
            for (int y = 0; y < H; y++) memcpy(base + (size_t)y * c.pitch, shadow + (size_t)y * W, (size_t)W * 4);
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
            strcpy(shown, status);
            first = 0;
            printf("rescue-screen: %dx%d, %s / %s\n", W, H, wifi, batt); fflush(stdout);
        }
        sleep(2);
    }
    return 0;
}
