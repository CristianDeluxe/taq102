// The networks a scan found, out of wpa_supplicant's SCAN_RESULTS table:
// one entry per SSID, the strongest of its access points, sorted by signal.
// Hidden networks (an empty SSID) are dropped; the security the flags
// declare decides whether the desk can offer to join.
#ifndef WIFI_SCAN_H
#define WIFI_SCAN_H

#include <stddef.h>

#define WIFI_SCAN_MAX 32
#define WIFI_SSID_MAX 33    // 32 bytes and a NUL, the standard's limit

enum wifi_security { WIFI_OPEN, WIFI_PSK, WIFI_SAE_ONLY, WIFI_EAP };

struct wifi_network {
    char ssid[WIFI_SSID_MAX];
    int level_dbm;
    enum wifi_security security;
};

struct wifi_scan {
    struct wifi_network network[WIFI_SCAN_MAX];
    int count;
};

// Parses the table (`bssid / frequency / signal level / flags / ssid`, a
// header line, tab-separated). Never fails on a malformed line: it is
// skipped. Returns the count.
int wifi_scan_parse(const char *text, size_t len, struct wifi_scan *out);

// Whether the desk can join it: open or WPA-PSK.
int wifi_scan_joinable(enum wifi_security security);

#endif
