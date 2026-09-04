// What the status bar reports: the battery as the rk816 driver sees it and
// the Wi-Fi link as the kernel reports it. Read once, drawn by whoever asks.
#ifndef STATUS_H
#define STATUS_H

struct status {
    int have_batt, cap, mv, ma;       // ma > 0 means current flows in
    char word[16];                    // the driver's status word, for a text line
    int have_wifi, level, quality;    // from /proc/net/wireless
    char addr[64];
};

void status_read(struct status *st);
// iOS lights three bars from about -55 dBm, two to about -70, one below.
int status_wifi_bars(const struct status *st);

#endif
