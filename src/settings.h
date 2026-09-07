#ifndef TAQ102_SETTINGS_H
#define TAQ102_SETTINGS_H

struct settings {
    int brightness_auto;
    int brightness;
    int sleep_minutes;
};

void settings_defaults(struct settings *settings);
int settings_load(struct settings *settings, const char *path);
/* Failures before rename preserve the old file. A later directory fsync
 * failure is reported, but the completed rename cannot be rolled back. */
int settings_save(const struct settings *settings, const char *path);
int settings_valid_sleep(int minutes);

#endif
