#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "status.h"

static int read_line(const char *path, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(out, (int)n, f) != NULL;
    if (ok) {
        size_t length = strlen(out);
        if (length && out[length - 1] == '\n') out[length - 1] = '\0';
        else if (!feof(f)) ok = 0;
    }
    fclose(f);
    return ok;
}

static int read_int(const char *path, int *value) {
    char text[32], *end;
    if (!read_line(path, text, sizeof text)) return 0;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno || end == text || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) return 0;
    *value = (int)parsed;
    return 1;
}

static int read_root_int(const char *root, const char *relative, int *value) {
    char path[256];
    int n = snprintf(path, sizeof path, "%s/%s", root, relative);
    return n > 0 && (size_t)n < sizeof path && read_int(path, value);
}

void status_power_read_at(struct status *st, const char *root, int64_t read_ms) {
    int voltage_uv = 0;
    char path[256];
    st->usb_online = st->ac_online = st->current_ua = 0;
    st->cap = st->mv = st->ma = 0;
    st->usb_valid = read_root_int(root, "usb/online", &st->usb_online) &&
                    (st->usb_online == 0 || st->usb_online == 1);
    st->ac_valid = read_root_int(root, "ac/online", &st->ac_online) &&
                   (st->ac_online == 0 || st->ac_online == 1);
    st->online_valid = st->usb_valid && st->ac_valid;
    // Unknown supply state is conservative so an incomplete snapshot cannot sleep the display.
    st->plugged = st->online_valid ? st->usb_online || st->ac_online : 1;
    st->current_valid = read_root_int(root, "battery/current_now", &st->current_ua);
    if (st->current_valid) st->ma = st->current_ua / 1000;
    st->cap_valid = read_root_int(root, "battery/capacity", &st->cap) &&
                    st->cap >= 0 && st->cap <= 100;
    st->have_batt = st->cap_valid;
    if (read_root_int(root, "battery/voltage_now", &voltage_uv)) st->mv = voltage_uv / 1000;
    int n = snprintf(path, sizeof path, "%s/battery/status", root);
    st->word_valid = n > 0 && (size_t)n < sizeof path && read_line(path, st->word, sizeof st->word);
    if (!st->word_valid) st->word[0] = '\0';
    st->read_ms = read_ms;
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

void status_read(struct status *st) {
    struct timespec now;
    memset(st, 0, sizeof *st);
    read_wifi(st);
    clock_gettime(CLOCK_MONOTONIC, &now);
    status_power_read_at(st, "/sys/class/power_supply",
                         (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

int status_wifi_bars(const struct status *st) {
    if (!st->have_wifi) return 0;
    return st->level >= -55 ? 3 : st->level >= -70 ? 2 : 1;
}
