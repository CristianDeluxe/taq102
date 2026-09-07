#include "backlight.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_level(const char *path) {
    FILE *file = fopen(path, "r");
    char text[32], *end;
    long value;
    if (!file) return -1;
    int ok = fgets(text, sizeof text, file) != NULL;
    if (ok && !strchr(text, '\n') && !feof(file)) ok = 0;
    fclose(file);
    if (!ok) return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    while (*end == '\n' || *end == '\r') ++end;
    if (errno || end == text || *end || value < 0 || value > INT_MAX) return -1;
    return (int)value;
}

int backlight_open_at(struct backlight *backlight, const char *root) {
    DIR *directory = opendir(root);
    struct dirent *entry;
    if (!directory) return -1;
    while ((entry = readdir(directory)) != NULL) {
        char max_path[256];
        if (entry->d_name[0] == '.') continue;
        int n = snprintf(backlight->dir, sizeof backlight->dir, "%s/%s", root, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof backlight->dir) continue;
        n = snprintf(max_path, sizeof max_path, "%s/max_brightness", backlight->dir);
        if (n <= 0 || (size_t)n >= sizeof max_path) continue;
        backlight->max = read_level(max_path);
        if (backlight->max >= 8) {
            closedir(directory);
            return 0;
        }
    }
    closedir(directory);
    return -1;
}

int backlight_open(struct backlight *backlight) {
    return backlight_open_at(backlight, "/sys/class/backlight");
}

int backlight_get(const struct backlight *backlight) {
    char path[256];
    int n = snprintf(path, sizeof path, "%s/brightness", backlight->dir);
    return n > 0 && (size_t)n < sizeof path ? read_level(path) : -1;
}

int backlight_set(const struct backlight *backlight, int level) {
    char path[256];
    if (level < 8) level = 8;
    if (level > backlight->max) level = backlight->max;
    int n = snprintf(path, sizeof path, "%s/brightness", backlight->dir);
    if (n <= 0 || (size_t)n >= sizeof path) return -1;
    FILE *file = fopen(path, "w");
    if (!file) return -1;
    int written = fprintf(file, "%d\n", level) > 0;
    int closed = fclose(file) == 0;
    return written && closed ? 0 : -1;
}
