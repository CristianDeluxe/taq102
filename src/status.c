#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "status.h"

static void read_line(const char *path, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0;
    fclose(f);
}

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

void status_read(struct status *st) {
    memset(st, 0, sizeof *st);
    read_wifi(st);
    read_battery(st);
}

int status_wifi_bars(const struct status *st) {
    if (!st->have_wifi) return 0;
    return st->level >= -55 ? 3 : st->level >= -70 ? 2 : 1;
}
