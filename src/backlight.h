#ifndef BACKLIGHT_H
#define BACKLIGHT_H

struct backlight {
    char dir[128];
    int max;
};

int backlight_open(struct backlight *backlight);
int backlight_open_at(struct backlight *backlight, const char *root);
int backlight_get(const struct backlight *backlight);
int backlight_set(const struct backlight *backlight, int level);

#endif
