#ifndef TOUCH_FLIP_H
#define TOUCH_FLIP_H

enum touch_axis {
    TOUCH_AXIS_X,
    TOUCH_AXIS_Y,
};

struct touch_flip {
    int width;
    int height;
    unsigned axes;
    const char *name;
};

int touch_flip_configure(struct touch_flip *config, int width, int height,
                         const char *mode);
int touch_flip_value(const struct touch_flip *config, enum touch_axis axis,
                     int value, int flipped);

#endif
