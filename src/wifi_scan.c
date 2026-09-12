#include "wifi_scan.h"

#include <stdlib.h>
#include <string.h>

static enum wifi_security security_of(const char *flags) {
    int psk = strstr(flags, "-PSK") != NULL;
    int sae = strstr(flags, "SAE") != NULL;
    int eap = strstr(flags, "EAP") != NULL;
    if (psk)
        return WIFI_PSK;
    if (sae)
        return WIFI_SAE_ONLY;
    if (eap)
        return WIFI_EAP;
    return WIFI_OPEN;
}

static int printable(const char *s) {
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || *s == 0x7f)
            return 0;
    return 1;
}

static int compare_level(const void *a, const void *b) {
    const struct wifi_network *x = a, *y = b;
    return y->level_dbm - x->level_dbm;
}

int wifi_scan_parse(const char *text, size_t len, struct wifi_scan *out) {
    memset(out, 0, sizeof *out);
    if (!text)
        return 0;
    size_t pos = 0;
    int first = 1;
    while (pos < len) {
        const char *line = text + pos;
        size_t n = 0;
        while (pos + n < len && line[n] != '\n')
            n++;
        pos += n + 1;
        if (first) {            // the header
            first = 0;
            continue;
        }
        // Fields: bssid, frequency, signal, flags, ssid; the SSID may carry
        // spaces, so the split is on tabs only.
        char copy[512];
        if (n >= sizeof copy)
            continue;
        memcpy(copy, line, n);
        copy[n] = '\0';
        char *fields[5] = { 0 };
        int f = 0;
        char *p = copy;
        while (f < 5) {
            fields[f++] = p;
            char *tab = strchr(p, '\t');
            if (!tab)
                break;
            *tab = '\0';
            p = tab + 1;
        }
        if (f < 5 || !fields[4] || !fields[4][0])
            continue;
        const char *ssid = fields[4];
        if (strlen(ssid) >= WIFI_SSID_MAX || !printable(ssid))
            continue;
        char *end;
        long level = strtol(fields[2], &end, 10);
        if (end == fields[2] || level > 0 || level < -120)
            continue;
        enum wifi_security sec = security_of(fields[3]);
        // The strongest access point stands for the SSID.
        int found = -1;
        for (int i = 0; i < out->count; i++)
            if (strcmp(out->network[i].ssid, ssid) == 0)
                found = i;
        if (found >= 0) {
            if (level > out->network[found].level_dbm) {
                out->network[found].level_dbm = (int)level;
                out->network[found].security = sec;
            }
            continue;
        }
        if (out->count >= WIFI_SCAN_MAX)
            continue;
        struct wifi_network *w = &out->network[out->count++];
        strcpy(w->ssid, ssid);
        w->level_dbm = (int)level;
        w->security = sec;
    }
    qsort(out->network, (size_t)out->count, sizeof out->network[0], compare_level);
    return out->count;
}

int wifi_scan_joinable(enum wifi_security security) {
    return security == WIFI_OPEN || security == WIFI_PSK;
}
