#ifndef TOUCH_FLIP_H
#define TOUCH_FLIP_H

enum touch_axis {
    TOUCH_AXIS_X,
    TOUCH_AXIS_Y,
};

struct touch_flip {
    int width;
    int height;
    float scale_x;   /* raw controller units to screen pixels */
    float scale_y;
    unsigned axes;
    const char *name;
};

/*
 * declared_x/declared_y are the controller's ABS_MT_POSITION maxima, so raw
 * values are scaled onto width x height before anything else looks at them.
 * The vendor 4.4 driver reported in screen pixels already (its closed
 * gsl_alg_id library did the conversion), so the maxima were 1023x599 and
 * the scale 1.0; mainline's silead.c reports the controller's own units.
 * A non-positive maximum means "unknown", and the scale stays 1.0.
 */
int touch_flip_configure(struct touch_flip *config, int width, int height,
                         int declared_x, int declared_y, const char *mode);
int touch_flip_value(const struct touch_flip *config, enum touch_axis axis,
                     int value, int flipped);

#endif
