// The rescue image's face: an amber screen that says RESCUE MODE, with the
// kernel, the build id and the Wi-Fi address, drawn straight to a DRM dumb
// buffer. It exists because a rescue that does not draw looks exactly like an
// appliance that failed -- under the stock kernel nothing sets a mode until an
// application does, and the panel just shows backlight.
//
// One buffer, no page flips: the picture changes only when the address does,
// and a full redraw once every two seconds is nothing. Runs until killed;
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

#define AMBER 0x00E08A00u
#define INK   0x00201000u
#define PALE  0x00FFF3D0u

// 3x5 glyphs, three bits per row, bit 2 the left column: A..Z, 0..9, then
// '.', ':', '-', '/'. Anything else advances without drawing.
static const unsigned char GLYPH[40][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,7,4,7},{7,4,7,4,4},
    {7,4,5,5,7},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,7},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{6,5,5,5,5},{7,5,5,5,7},{7,5,7,4,4},{7,5,5,7,1},{7,5,6,5,5},
    {7,4,7,1,7},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
    {0,0,0,0,2},{0,2,0,2,0},{0,0,7,0,0},{1,1,2,4,4}
};

static int glyph_index(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
    switch (ch) { case '.': return 36; case ':': return 37; case '-': return 38; case '/': return 39; }
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

static volatile sig_atomic_t stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

static void paint(uint32_t *shadow, int W, int H, const char *kernel, const char *build, const char *addr) {
    for (int i = 0; i < W * H; i++) shadow[i] = AMBER;
    int big = W / 60;                       // "RESCUE MODE" is 11 glyphs, 4 units each
    text(shadow, W, H, (W - 11 * 4 * big + big) / 2, H / 5, "RESCUE MODE", big, INK);
    int s = W / 200;
    int y = H / 2, dy = 7 * s;
    char line[96];
    snprintf(line, sizeof line, "KERNEL %s", kernel); text(shadow, W, H, W / 12, y, line, s, INK); y += dy;
    snprintf(line, sizeof line, "BUILD %s", build);   text(shadow, W, H, W / 12, y, line, s, INK); y += dy;
    snprintf(line, sizeof line, "SSH ROOT AT %s", addr); text(shadow, W, H, W / 12, y, line, s, PALE); y += dy;
    text(shadow, W, H, W / 12, y, "USB CONSOLE TTYGS0 - KILLALL RESCUE-SCREEN TO DRAW", s, INK);
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
    char build[64], addr[64], shown[64] = "";
    read_line("/etc/taq102-build-id", build, sizeof build);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    int first = 1;
    while (!stop) {
        wlan_address(addr, sizeof addr);
        if (first || strcmp(addr, shown)) {
            paint(shadow, W, H, u.release, build, addr);
            for (int y = 0; y < H; y++) memcpy(base + (size_t)y * c.pitch, shadow + (size_t)y * W, (size_t)W * 4);
            if (first && drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) {
                perror("setcrtc"); return 1;
            }
            strcpy(shown, addr);
            first = 0;
            printf("rescue-screen: %dx%d, %s\n", W, H, addr); fflush(stdout);
        }
        sleep(2);
    }
    return 0;
}
