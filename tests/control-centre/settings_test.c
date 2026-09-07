// SOURCES: settings.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "settings.h"

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static void assert_text(const char *path, const char *expected) {
    char actual[256];
    FILE *file = fopen(path, "r");
    assert(file);
    size_t length = fread(actual, 1, sizeof actual - 1, file);
    assert(!ferror(file) && fclose(file) == 0);
    actual[length] = '\0';
    assert(strcmp(actual, expected) == 0);
}

int main(void) {
    char dir[] = "/tmp/taq102-audit/settings-XXXXXX"; assert(mkdtemp(dir));
    char path[256]; snprintf(path, sizeof path, "%s/taq102.conf", dir);
    struct settings s; settings_defaults(&s);
    assert(s.brightness_auto == 1 && s.brightness == 200 && s.sleep_minutes == 5);
    assert(settings_load(&s, path) == -1 && s.brightness == 200);          // missing file: defaults
    s.brightness_auto = 0; s.brightness = 8; s.sleep_minutes = 0;
    assert(settings_load(&s, path) == -1);
    assert(s.brightness_auto == 1 && s.brightness == 200 && s.sleep_minutes == 5);
    write_text(path,
        "brightness=abc\nbrightness=7\nbrightness=256\nbrightness=96junk\n"
        "sleep_minutes=-1\nsleep_minutes=7\nbrightness_auto=2\n"
        "brightness_auto=0\nbrightness_auto=1\nunknown=12\n\n# c\n");
    assert(settings_load(&s, path) == 0);
    assert(s.brightness == 200 && s.sleep_minutes == 5 && s.brightness_auto == 1); // invalid kept default, last key wins
    s.brightness = 96; s.sleep_minutes = 15; s.brightness_auto = 0;
    assert(settings_save(&s, path) == 0);
    struct settings r; settings_defaults(&r); assert(settings_load(&r, path) == 0);
    assert(r.brightness == 96 && r.sleep_minutes == 15 && r.brightness_auto == 0);

    write_text(path,
        "brightness=8\nbrightness=255\nbrightness=9\n"
        "sleep_minutes=0\nsleep_minutes=1\nsleep_minutes=5\n"
        "brightness_auto=1\nbrightness_auto=0\n");
    settings_defaults(&r); assert(settings_load(&r, path) == 0);
    assert(r.brightness == 9 && r.sleep_minutes == 5 && r.brightness_auto == 0);

    write_text(path,
        "brightness=96\nbrightness=bad\n"
        "sleep_minutes=15\nsleep_minutes=7\n"
        "brightness_auto=0\nbrightness_auto=2\n");
    settings_defaults(&r); assert(settings_load(&r, path) == 0);
    assert(r.brightness == 200 && r.sleep_minutes == 5 && r.brightness_auto == 1);

    write_text(path, "brightness_auto=0\r\nbrightness=8\r\nsleep_minutes=15\r\n");
    settings_defaults(&r); assert(settings_load(&r, path) == 0);
    assert(r.brightness_auto == 0 && r.brightness == 8 && r.sleep_minutes == 15);

    char long_line[600];
    memset(long_line, 'x', sizeof long_line);
    memcpy(long_line, "brightness=", 11);
    long_line[sizeof long_line - 2] = '\n'; long_line[sizeof long_line - 1] = '\0';
    write_text(path, long_line);
    settings_defaults(&r); assert(settings_load(&r, path) == 0);
    assert(r.brightness == 200);

    memset(long_line, 'x', sizeof long_line);
    memcpy(long_line + 300, "brightness=8\n", 13);
    memcpy(long_line + 313, "sleep_minutes=1\n", 16);
    long_line[329] = '\0';
    write_text(path, long_line);
    settings_defaults(&r); assert(settings_load(&r, path) == 0);
    assert(r.brightness == 200 && r.sleep_minutes == 1);

    write_text(path, "brightness_auto=0");
    settings_defaults(&r); assert(settings_load(&r, path) == 0);
    assert(r.brightness_auto == 0);

    r.brightness_auto = 0; r.brightness = 8; r.sleep_minutes = 0;
    assert(settings_load(&r, dir) == -1);
    assert(r.brightness_auto == 1 && r.brightness == 200 && r.sleep_minutes == 5);

    char old_dir[256]; snprintf(old_dir, sizeof old_dir, "%s/existing", dir);
    assert(mkdir(old_dir, 0755) == 0);
    char marker[256]; snprintf(marker, sizeof marker, "%s/marker", old_dir);
    write_text(marker, "old file remains\n");
    assert(settings_save(&s, old_dir) == -1);
    assert_text(marker, "old file remains\n");

    char bad[256]; snprintf(bad, sizeof bad, "%s/nodir/taq102.conf", dir);
    assert(settings_save(&s, bad) == -1);
    assert(settings_valid_sleep(0) && settings_valid_sleep(1));
    assert(settings_valid_sleep(5) && settings_valid_sleep(15));
    assert(!settings_valid_sleep(-1) && !settings_valid_sleep(7));
    printf("settings ok\n"); return 0;
}
