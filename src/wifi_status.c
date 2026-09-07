#include "wifi_status.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int wifi_read_ssid(const char *conf_path, char *out, size_t n) {
    if (!out || !n) return -1;
    out[0] = '\0';
    if (!conf_path) return -1;
    int fd = open(conf_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    FILE *f = fdopen(fd, "r");
    if (!f) { close(fd); return -1; }
    char line[4096];
    int result = -1;
    while (fgets(line, sizeof line, f)) {
        if (!strchr(line, '\n') && !feof(f)) break;
        const char *p = line;
        while (isspace((unsigned char)*p)) ++p;
        if (strncmp(p, "ssid", 4)) continue;
        p += 4;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p++ != '=') continue;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p++ != '"') break;
        char value[33]; size_t used = 0;
        int invalid = 0;
        while (*p && *p != '"') {
            unsigned char ch = (unsigned char)*p++;
            if (ch == '\\') {
                if (!*p) { invalid = 1; break; }
                ch = (unsigned char)*p++;
                if (ch != '\\' && ch != '"') { invalid = 1; break; }
            }
            if (ch < 32 || used == 32) { invalid = 1; break; }
            value[used++] = (char)ch;
        }
        if (invalid || *p != '"' || used == 0 || used >= n) break;
        ++p;
        while (isspace((unsigned char)*p)) ++p;
        if (*p && *p != '#') break;
        value[used] = '\0'; memcpy(out, value, used + 1); result = 0; break;
    }
    fclose(f);
    return result;
}

enum wifi_state wifi_state_from(const struct status *st, int worker_busy, int last_exit, int wanted_on) {
    if (worker_busy) return WIFI_STARTING;
    if (last_exit != 0) return WIFI_FAILED;
    if (!wanted_on) return WIFI_OFF;
    struct in_addr addr;
    if (st && st->have_wifi && memchr(st->addr, '\0', sizeof st->addr) &&
        inet_pton(AF_INET, st->addr, &addr) == 1 && addr.s_addr != INADDR_ANY &&
        addr.s_addr != INADDR_BROADCAST)
        return WIFI_CONNECTED;
    return WIFI_ASSOCIATING;
}
