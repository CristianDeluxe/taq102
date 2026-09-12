// The supplicant's configuration file on /data: the networks the tablet
// knows. Reading lists their SSIDs and priorities; writing adds or replaces
// one block and keeps every other line byte for byte, through a temporary
// file and a rename, so a failure half-way leaves the old file whole. The
// passphrase goes in quoted, as wpa_supplicant reads it, and never anywhere
// else.
#ifndef WIFI_CONF_H
#define WIFI_CONF_H

#include <stddef.h>

#define WIFI_CONF_MAX_NETWORKS 32
#define WIFI_CONF_SSID_MAX 33

struct wifi_known {
    char ssid[WIFI_CONF_SSID_MAX];
    int priority;       // 0 when the block has none
};

struct wifi_conf {
    struct wifi_known network[WIFI_CONF_MAX_NETWORKS];
    int count;
};

// Reads the blocks. A missing file is an empty list and returns 0; a file
// that cannot be read returns -1.
int wifi_conf_read(const char *path, struct wifi_conf *out);

// Whether `ssid` has a block.
int wifi_conf_knows(const struct wifi_conf *conf, const char *ssid);

// Writes a block for `ssid`: `psk` quoted when given, `key_mgmt=NONE` when
// NULL (an open network), with `priority`. A block with the same SSID is
// replaced. The passphrase must be 8..63 printable ASCII when given.
// 0 on success, -1 with the reason on stderr.
int wifi_conf_write_block(const char *path, const char *ssid, const char *psk, int priority);

// Removes the block for `ssid`. 0 when removed or absent, -1 on error.
int wifi_conf_remove(const char *path, const char *ssid);

// The highest priority among the known networks, or 0.
int wifi_conf_top_priority(const struct wifi_conf *conf);

#endif
