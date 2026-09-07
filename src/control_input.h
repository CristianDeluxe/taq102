#ifndef CONTROL_INPUT_H
#define CONTROL_INPUT_H
#include "control_runtime.h"
#include "touch_flip.h"
#include "touch_input.h"
#include "touch_router.h"

struct control_input {
    struct touch_input *decoder;
    struct touch_router *router;
    struct cube_contacts cube;
    int cube_released, failed;
};
int control_input_init(struct control_input *input, const struct touch_flip *flip);
void control_input_feed(struct control_input *input, struct control_runtime *r,
                        const struct touch_event *events, int count);
int control_input_read(struct control_input *input, struct control_runtime *r, int fd, double now);
void control_input_reset(struct control_input *input, struct control_runtime *r, int flipped);
void control_input_free(struct control_input *input);
#endif
