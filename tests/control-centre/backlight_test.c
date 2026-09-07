// SOURCES: backlight.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "backlight.h"

static void write_value(const char *path, const char *value) {
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs(value, file) >= 0);
    assert(fclose(file) == 0);
}

int main(void) {
    struct backlight backlight;
    char root[] = "/tmp/taq102-audit/backlight-XXXXXX";
    char panel[256], max_path[256], brightness_path[256];
    assert(mkdtemp(root) != NULL);
    snprintf(panel, sizeof panel, "%s/panel", root);
    assert(mkdir(panel, 0700) == 0);
    snprintf(max_path, sizeof max_path, "%s/max_brightness", panel);
    snprintf(brightness_path, sizeof brightness_path, "%s/brightness", panel);
    assert(backlight_open_at(&backlight, root) == -1);
    write_value(max_path, "bad\n");
    assert(backlight_open_at(&backlight, root) == -1);
    write_value(max_path, "7\n");
    assert(backlight_open_at(&backlight, root) == -1);
    write_value(max_path, "255\n");
    write_value(brightness_path, "100\n");
    assert(backlight_open_at(&backlight, root) == 0);
    assert(backlight.max == 255 && backlight_get(&backlight) == 100);
    assert(backlight_set(&backlight, -5) == 0 && backlight_get(&backlight) == 8);
    assert(backlight_set(&backlight, 999) == 0 && backlight_get(&backlight) == 255);
    puts("backlight ok");
    return 0;
}
