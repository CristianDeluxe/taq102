#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include "touch_flip.h"

enum touch_kind {
    TOUCH_DOWN,
    TOUCH_MOVE,
    TOUCH_UP,
    TOUCH_CANCEL,
};

struct touch_event {
    enum touch_kind kind;
    int slot;
    float x;
    float y;
    double t;
};

#define TOUCH_MAX_SLOTS 10

struct touch_raw {
    unsigned short type;
    unsigned short code;
    int value;
    double t;
};

struct touch_input;

struct touch_input *touch_input_new(const struct touch_flip *flip);
void touch_input_free(struct touch_input *ti);
/* A changed orientation queues cancellation of every visible contact. */
void touch_input_set_flipped(struct touch_input *ti, int flipped);
/* Pass n == 0 to drain queued events. A small max never discards the rest. */
int touch_input_feed(struct touch_input *ti, const struct touch_raw *raw, int n,
                     struct touch_event *out, int max);
/* now uses CLOCK_MONOTONIC seconds, matching read_fd's selected event clock. */
int touch_input_expire(struct touch_input *ti, double now,
                       struct touch_event *out, int max);
int touch_input_cancel_all(struct touch_input *ti, struct touch_event *out,
                           int max);
/*
 * fd must be nonblocking and remain open for this object's lifetime. The first
 * call selects CLOCK_MONOTONIC with EVIOCSCLOCKID; failure reads no records and
 * can be retried. A later call with another fd fails with EINVAL. Unsupported
 * outside Linux.
 */
int touch_input_read_fd(struct touch_input *ti, int fd, struct touch_event *out,
                        int max);

#endif
