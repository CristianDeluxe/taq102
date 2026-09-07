// What the status bar reports: the battery as the rk816 driver sees it and
// the Wi-Fi link as the kernel reports it. Read once, drawn by whoever asks.
#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>

struct status {
    int have_batt, cap, mv, ma;       // ma > 0 means current flows in
    int plugged;                      // a charger is on the USB port
    char word[16];                    // the driver's status word, for a text line
    int have_wifi, level, quality;    // from /proc/net/wireless
    char addr[64];
    int usb_online, ac_online;
    int usb_valid, ac_valid, online_valid;
    int current_ua, current_valid, cap_valid, word_valid;
    int64_t read_ms;
};

void status_read(struct status *st);
void status_power_read_at(struct status *st, const char *root, int64_t read_ms);
// iOS lights three bars from about -55 dBm, two to about -70, one below.
int status_wifi_bars(const struct status *st);

#endif
