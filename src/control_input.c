#include "control_input.h"
#include <math.h>
#include <string.h>
#include "control_center_layout.h"

int control_input_init(struct control_input *input, const struct touch_flip *flip) {
    memset(input, 0, sizeof *input);
    input->decoder = touch_input_new(flip);
    input->router = touch_router_new(flip->width, flip->height, 48,
        CC_PANEL.x, CC_PANEL.y, CC_PANEL.w, CC_PANEL.h);
    if (!input->decoder || !input->router) { control_input_free(input); return -1; }
    return 0;
}

void control_input_feed(struct control_input *input, struct control_runtime *r,
                        const struct touch_event *events, int count) {
    for (int i = 0; i < count && !r->sleep_requested; i++) {
        const struct touch_event *ev = &events[i];
        if (!isfinite(ev->t) || ev->t < 0 || ev->t >= (double)INT64_MAX / 1000.0) continue;
        int64_t now = (int64_t)(ev->t * 1000.0);
        if (ev->kind == TOUCH_DOWN || ev->kind == TOUCH_MOVE)
            sleep_timer_touch(&r->idle, now);
        if ((ev->kind == TOUCH_UP || ev->kind == TOUCH_CANCEL) &&
            ev->slot >= 0 && ev->slot < TOUCH_MAX_SLOTS && input->cube.slot[ev->slot].active)
            input->cube_released = 1;
        cc_tick(&r->panel, now);
        touch_router_set_panel_open(input->router, cc_visible(&r->panel));
        struct router_out out;
        touch_router_feed(input->router, ev, 1, &out);
        for (;;) {
            if (out.n_panel < 0) { input->failed = 1; return; }
            if (out.open_panel) {
                cc_open(&r->panel, now);
                touch_router_set_panel_open(input->router, 1);
            }
            for (int j = 0; j < out.n_panel; j++) {
                struct cc_result actions[3];
                int n = cc_handle(&r->panel, &out.panel[j], now, actions, 3);
                for (int k = 0; k < n; k++) control_runtime_action(r, actions[k], now);
            }
            if (out.close_panel) cc_close(&r->panel, now);
            input->cube = out.cube;
            if (out.n_panel < 32) break;
            touch_router_feed(input->router, NULL, 0, &out);
        }
    }
}

int control_input_read(struct control_input *input, struct control_runtime *r, int fd, double now) {
    struct touch_event events[64];
    input->cube_released = 0;
    int n;
    if (fd >= 0) {
        n = touch_input_read_fd(input->decoder, fd, events, 64);
        if (n < 0) return -1;
        control_input_feed(input, r, events, n);
        while (n == 64 && !r->sleep_requested) {
            n = touch_input_feed(input->decoder, NULL, 0, events, 64);
            if (n < 0) return -1;
            control_input_feed(input, r, events, n);
        }
    }
    n = touch_input_expire(input->decoder, now, events, 64);
    if (n < 0) return -1;
    control_input_feed(input, r, events, n);
    while (n == 64 && !r->sleep_requested) {
        n = touch_input_feed(input->decoder, NULL, 0, events, 64);
        if (n < 0) return -1;
        control_input_feed(input, r, events, n);
    }
    return input->failed ? -1 : 0;
}

void control_input_reset(struct control_input *input, struct control_runtime *r, int flipped) {
    struct touch_event discarded[64];
    touch_input_set_flipped(input->decoder, flipped);
    touch_input_cancel_all(input->decoder, discarded, 64);
    while (touch_input_feed(input->decoder, NULL, 0, discarded, 64) > 0) {}
    touch_router_reset(input->router);
    memset(&input->cube, 0, sizeof input->cube);
    input->cube_released = 1;
    if (r->panel.capture_slot >= 0) {
        struct touch_event cancel = { .kind = TOUCH_CANCEL, .slot = r->panel.capture_slot };
        struct cc_result actions[3];
        cc_handle(&r->panel, &cancel, 0, actions, 3);
    }
    r->panel.armed = 0;
    r->panel.dirty = 1;
}
void control_input_free(struct control_input *input) {
    touch_input_free(input->decoder);
    touch_router_free(input->router);
}
