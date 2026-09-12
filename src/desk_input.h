// Which thing a contact belongs to: the rail, the bank selector, the lock
// target, or the content. A contact is owned from down to up by what it
// landed on, and a rail or bank tap completes only when the finger lifts on
// the same entry it pressed. Pure; the model and the lock are the caller's.
#ifndef DESK_INPUT_H
#define DESK_INPUT_H

#include "desk_model.h"
#include "touch_input.h"

enum desk_target { TARGET_NONE, TARGET_RAIL, TARGET_BANK, TARGET_LOCK, TARGET_CONTENT };

struct desk_input {
    struct {
        enum desk_target target;
        int index;          // rail entry or bank pill pressed
    } slot[TOUCH_MAX_SLOTS];
};

// What lies under a point for the model's current view: a rail entry only
// for a page that exists, a bank pill only when the page has more than one.
enum desk_target desk_input_target(const struct desk_model *m, int x, int y, int *index);

void desk_input_init(struct desk_input *in);

// Feeds one event. Returns the target that owns the contact, and on an UP
// that completes a rail or bank tap, sets `*index` and returns that target;
// otherwise `*index` is -1. Content events are the caller's to pass to the
// model; lock events go to the lock.
enum desk_target desk_input_feed(struct desk_input *in, const struct desk_model *m,
                                 const struct touch_event *ev, int *index);

#endif
