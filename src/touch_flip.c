#include "touch_flip.h"

#include <string.h>

#define FLIP_X 1u
#define FLIP_Y 2u

int touch_flip_configure(struct touch_flip *config, int width, int height,
                         const char *mode) {
    config->width = width;
    config->height = height;

    if (!mode || strcmp(mode, "xy") == 0) {
        config->axes = FLIP_X | FLIP_Y;
        config->name = "xy";
        return 0;
    }
    if (strcmp(mode, "x") == 0) {
        config->axes = FLIP_X;
        config->name = "x";
        return 0;
    }
    if (strcmp(mode, "y") == 0) {
        config->axes = FLIP_Y;
        config->name = "y";
        return 0;
    }
    if (strcmp(mode, "none") == 0) {
        config->axes = 0;
        config->name = "none";
        return 0;
    }

    config->axes = FLIP_X | FLIP_Y;
    config->name = "xy";
    return -1;
}

int touch_flip_value(const struct touch_flip *config, enum touch_axis axis,
                     int value, int flipped) {
    if (!flipped) return value;
    if (axis == TOUCH_AXIS_X && (config->axes & FLIP_X))
        return config->width - value;
    if (axis == TOUCH_AXIS_Y && (config->axes & FLIP_Y))
        return config->height - value;
    return value;
}
