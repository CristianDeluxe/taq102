// The rescue image's face: an amber screen that says RESCUE MODE, with the
// kernel, the build id, the Wi-Fi address and signal and the battery, drawn
// straight to a DRM dumb buffer. It exists because a rescue that does not
// draw looks exactly like an appliance that failed -- under the stock kernel
// nothing sets a mode until an application does, and the panel just shows
// backlight.
//
// Top right, an iOS-style status bar: a three-arc Wi-Fi fan lit by signal
// level, and a battery with its charge as a fill, a bolt while current flows
// in, and the percentage beside it. Everything is drawn by pixel tests into a
// shadow buffer, so there is no font or image file to ship.
//
// One buffer, no page flips: the picture changes only when a status line
// does, and a full redraw once every two seconds is nothing. Runs until
// killed; `killall rescue-screen` frees the display for whatever is being
// debugged. RESCUE_DUMP=<file.ppm> writes each painted frame there, so the
// screen can be checked without a camera.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
#define DIM   0xFFB8741Cu   // unlit arcs and the status bar: amber, darkened
#define GREEN 0xFF30C048u   // iOS charging green
#define RED   0xFFE03030u   // iOS low-battery red

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

// The charging bolt, 5 wide by 7 tall, bit 4 the left column.
static const unsigned char BOLT[7] = { 0x03, 0x06, 0x0C, 0x1F, 0x06, 0x0C, 0x18 };

struct canvas { uint32_t *px; int w, h; };

static void put(struct canvas *c, int x, int y, uint32_t col) {
    if (x >= 0 && x < c->w && y >= 0 && y < c->h) c->px[y * c->w + x] = col;
}

static void fill_rect(struct canvas *c, int x, int y, int w, int h, uint32_t col) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            put(c, x + i, y + j, col);
}

// A rectangle with rounded corners of radius r, filled.
static void round_rect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t col) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int cx = i < r ? r : (i >= w - r ? w - 1 - r : i);
            int cy = j < r ? r : (j >= h - r ? h - 1 - r : j);
            float dx = i - cx, dy = j - cy;
            if (dx * dx + dy * dy <= (float)r * r + 0.5f) put(c, x + i, y + j, col);
        }
}

static int glyph_index(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
    switch (ch) { case '.': return 36; case ':': return 37; case '-': return 38; case '/': return 39; case '%': return 40; }
    return -1;
}

static void text(struct canvas *c, int x, int y, const char *str, int s, uint32_t col) {
    for (const char *p = str; *p; p++, x += 4 * s) {
        int g = glyph_index(*p);
        if (g < 0) continue;
        for (int r = 0; r < 5; r++)
            for (int k = 0; k < 3; k++)
                if (GLYPH[g][r] & (4 >> k))
                    fill_rect(c, x + k * s, y + r * s, s, s, col);
    }
}

static int text_width(const char *str, int s) { return (int)strlen(str) * 4 * s - s; }

// The Wi-Fi fan: a dot and three arcs of a 90-degree sector opening upward
// from the apex at (ax, ay). `lit` arcs from the inside out take INK, the
// rest DIM, as iOS greys out the bars it does not have.
static void wifi_fan(struct canvas *c, int ax, int ay, int size, int lit) {
    float unit = size / 4.f, thick = unit * 0.55f;
    for (int y = ay - size; y <= ay; y++)
        for (int x = ax - size; x <= ax + size; x++) {
            float dx = x - ax, dy = ay - y;
            if (dy < 0 || fabsf(dx) > dy) continue;      // 45 degrees each side
            float r = sqrtf(dx * dx + dy * dy);
            int ring = -1;
            if (r <= unit * 0.55f) ring = 0;
            else for (int k = 1; k <= 3; k++)
                if (r <= unit * (k + 0.5f) && r > unit * (k + 0.5f) - thick) ring = k;
            if (ring < 0) continue;
            put(c, x, y, ring <= lit ? INK : DIM);
        }
}

// The battery: an outline with a nub, the charge as a fill, and a bolt
// while current flows in. (x, y) is the top-left of the body.
static void battery_icon(struct canvas *c, int x, int y, int w, int h, int cap, int charging) {
    int r = h / 4, line = h / 9 > 1 ? h / 9 : 1, gap = line;
    int inner_r = r - line > 0 ? r - line : 1;
    round_rect(c, x, y, w, h, r, INK);
    round_rect(c, x + line, y + line, w - 2 * line, h - 2 * line, inner_r, AMBER);
    fill_rect(c, x + w, y + h / 3, line + 1, h / 3, INK);           // the nub
    int inner_w = w - 2 * (line + gap), inner_h = h - 2 * (line + gap);
    int fill_w = inner_w * (cap < 0 ? 0 : cap > 100 ? 100 : cap) / 100;
    uint32_t col = charging ? GREEN : cap <= 20 ? RED : INK;
    if (fill_w > 0)
        round_rect(c, x + line + gap, y + line + gap, fill_w, inner_h, inner_r, col);
    if (charging) {
        int s = inner_h / 7 > 1 ? inner_h / 7 : 1;
        int bx = x + w / 2 - 5 * s / 2, by = y + h / 2 - 7 * s / 2;
        for (int row = 0; row < 7; row++)
            for (int k = 0; k < 5; k++)
                if (BOLT[row] & (16 >> k)) fill_rect(c, bx + k * s, by + row * s, s, s, PALE);
    }
}

static void read_line(const char *path, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0;
    fclose(f);
}

struct status {
    int have_batt, cap, mv, ma;       // ma > 0 means current flows in
    char word[16];                    // the driver's status word, for the text line
    int have_wifi, level, quality;    // from /proc/net/wireless
    char addr[64];
};

// The rk816 driver's view, which is what decides whether the tablet is about
// to switch off. current_now is negative while discharging, and it stayed
// negative on a hub with the status still saying Charging -- the number, not
// the word, is the truth, so the bolt follows the sign of the current.
static void read_battery(struct status *st) {
    char cap[16], vol[16], cur[16];
    read_line("/sys/class/power_supply/battery/capacity", cap, sizeof cap);
    read_line("/sys/class/power_supply/battery/voltage_now", vol, sizeof vol);
    read_line("/sys/class/power_supply/battery/current_now", cur, sizeof cur);
    read_line("/sys/class/power_supply/battery/status", st->word, sizeof st->word);
    st->have_batt = cap[0] != 0;
    st->cap = atoi(cap);
    st->mv = atoi(vol) / 1000;
    st->ma = atoi(cur) / 1000;
}

// Address from the interface, level and link quality from
// /proc/net/wireless, which prints "0000  100.  -37.  -256." -- integers
// each followed by a dot; %f would swallow "100." whole.
static void read_wifi(struct status *st) {
    struct ifreq ifr = { 0 };
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
    if (s >= 0 && ioctl(s, SIOCGIFADDR, &ifr) == 0)
        snprintf(st->addr, sizeof st->addr, "%s", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    else
        snprintf(st->addr, sizeof st->addr, "NO WIFI YET");
    if (s >= 0) close(s);

    st->have_wifi = 0;
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *w = strstr(line, "wlan0:");
        if (!w) continue;
        int q = 0, l = 0;
        if (sscanf(w + 6, " %*x %d. %d.", &q, &l) == 2) { st->quality = q; st->level = l; st->have_wifi = 1; }
    }
    fclose(f);
}

// iOS lights three bars from about -55 dBm, two to about -70, one below.
static int wifi_bars(const struct status *st) {
    if (!st->have_wifi) return 0;
    return st->level >= -55 ? 3 : st->level >= -70 ? 2 : 1;
}

static void status_bar(struct canvas *c, const struct status *st) {
    int s = c->w / 200;                     // the text scale used below
    int bar_h = 11 * s;
    int margin = 3 * s;
    fill_rect(c, 0, 0, c->w, bar_h, DIM);
    int icon_h = 5 * s, icon_w = 11 * s;
    int y = (bar_h - icon_h) / 2;
    int right = c->w - margin - icon_w - s - 1;
    battery_icon(c, right, y, icon_w, icon_h, st->have_batt ? st->cap : 0, st->have_batt && st->ma > 0);
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", st->have_batt ? st->cap : 0);
    right -= 2 * s + text_width(pct, s);
    text(c, right, (bar_h - 5 * s) / 2, pct, s, INK);
    right -= 4 * s + icon_h;
    wifi_fan(c, right, y + icon_h, icon_h + s, wifi_bars(st));
}

static void paint(struct canvas *c, const char *kernel, const char *build, const struct status *st) {
    for (int i = 0; i < c->w * c->h; i++) c->px[i] = AMBER;
    status_bar(c, st);
    int big = c->w / 60;                    // "RESCUE MODE" is 11 glyphs, 4 units each
    text(c, (c->w - 11 * 4 * big + big) / 2, c->h / 5, "RESCUE MODE", big, INK);
    int s = c->w / 200;
    int y = c->h / 2, dy = 7 * s, x = c->w / 12;
    char line[96];
    snprintf(line, sizeof line, "KERNEL %s", kernel); text(c, x, y, line, s, INK); y += dy;
    snprintf(line, sizeof line, "BUILD %s", build);   text(c, x, y, line, s, INK); y += dy;
    if (st->have_wifi) snprintf(line, sizeof line, "WIFI %s %dDBM Q%d", st->addr, st->level, st->quality);
    else snprintf(line, sizeof line, "WIFI %s", st->addr);
    text(c, x, y, line, s, PALE); y += dy;
    if (st->have_batt) snprintf(line, sizeof line, "BATTERY %d%% %d.%02dV %s %dMA", st->cap, st->mv / 1000, (st->mv / 10) % 100, st->word, st->ma);
    else snprintf(line, sizeof line, "BATTERY UNKNOWN");
    text(c, x, y, line, s, PALE); y += dy;
    text(c, x, y, "SSH ROOT - TTYGS0 - KILLALL RESCUE-SCREEN TO DRAW", s, INK);
}

static void dump_ppm(const struct canvas *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
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

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    int first = 1;
    while (!stop) {
        struct status st = { 0 };
        read_wifi(&st);
        read_battery(&st);
        snprintf(key, sizeof key, "%s|%d|%d|%d|%d|%d|%s", st.addr, st.have_wifi, st.level, st.quality, st.cap, st.ma > 0, st.word);
        if (first || strcmp(key, shown)) {
            paint(&canvas, u.release, build, &st);
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
    return 0;
}
