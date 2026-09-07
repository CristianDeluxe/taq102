// SOURCES: oneeuro.c touch_flip.c touch_input.c touch_router.c
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <linux/input.h>
#include <time.h>
#include <unistd.h>

static unsigned char fake_input[8 * sizeof(struct input_event)];
static size_t fake_input_length;
static size_t fake_input_offset;
static int fake_clock = -1;
static int fake_clock_error;

static ssize_t fake_linux_read(int fd, void *buffer, size_t length) {
    (void)fd;
    if (fake_input_offset == fake_input_length) {
        errno = EAGAIN;
        return -1;
    }
    if (length > 7) length = 7;
    size_t remaining = fake_input_length - fake_input_offset;
    if (length > remaining) length = remaining;
    memcpy(buffer, fake_input + fake_input_offset, length);
    fake_input_offset += length;
    return (ssize_t)length;
}

static int fake_linux_ioctl(int fd, unsigned long request, void *argument) {
    (void)fd;
    if (request != EVIOCSCLOCKID) {
        errno = EINVAL;
        return -1;
    }
    if (fake_clock_error) {
        errno = EIO;
        return -1;
    }
    fake_clock = *(const int *)argument;
    return 0;
}

struct touch_input_linux_io {
    ssize_t (*read_event)(int fd, void *buffer, size_t length);
    int (*ioctl_event)(int fd, unsigned long request, void *argument);
};

const struct touch_input_linux_io touch_input_linux_io = {
    fake_linux_read,
    fake_linux_ioctl,
};
#else
#include <linux/input-event-codes.h>
#endif

#include "touch_flip.h"
#include "touch_input.h"
#include "touch_router.h"

#define R(ty, co, va, tt) (struct touch_raw){(ty), (co), (va), (tt)}
#define E(ki, sl, xx, yy, tt) \
    (struct touch_event){(ki), (sl), (xx), (yy), (tt)}

static void test_input_frames(void) {
    struct touch_flip flip;
    assert(touch_flip_configure(&flip, 1024, 600, "xy") == 0);
    struct touch_input *ti = touch_input_new(&flip);
    assert(ti != NULL);
    struct touch_event ev[32];

    struct touch_raw tap[] = {
        R(EV_ABS, ABS_MT_SLOT, 0, 1.0f),
        R(EV_ABS, ABS_MT_TRACKING_ID, 7, 1.0f),
        R(EV_ABS, ABS_MT_POSITION_X, 500, 1.0f),
        R(EV_ABS, ABS_MT_POSITION_Y, 300, 1.0f),
        R(EV_SYN, SYN_REPORT, 0, 1.0f),
        R(EV_ABS, ABS_MT_TRACKING_ID, -1, 1.05f),
        R(EV_SYN, SYN_REPORT, 0, 1.05f),
    };
    int n = touch_input_feed(ti, tap, 7, ev, 32);
    assert(n == 2);
    assert(ev[0].kind == TOUCH_DOWN && ev[0].slot == 0);
    assert(ev[0].x == 500.f && ev[0].y == 300.f);
    assert(ev[1].kind == TOUCH_UP && ev[1].slot == 0);

    struct touch_raw no_slot[] = {
        R(EV_ABS, ABS_MT_TRACKING_ID, 8, 2.0f),
        R(EV_ABS, ABS_MT_POSITION_X, 10, 2.0f),
        R(EV_ABS, ABS_MT_POSITION_Y, 10, 2.0f),
        R(EV_SYN, SYN_REPORT, 0, 2.0f),
    };
    n = touch_input_feed(ti, no_slot, 4, ev, 32);
    assert(n == 1 && ev[0].kind == TOUCH_DOWN && ev[0].slot == 0);
    assert(ev[0].x == 10.f && ev[0].y == 10.f);

    struct touch_raw half[] = {
        R(EV_ABS, ABS_MT_POSITION_X, 20, 2.1f),
    };
    assert(touch_input_feed(ti, half, 1, ev, 32) == 0);
    assert(touch_input_expire(ti, 2.6f, ev, 32) == 0);
    n = touch_input_expire(ti, 2.6001f, ev, 32);
    assert(n == 1 && ev[0].kind == TOUCH_CANCEL);

    touch_input_free(ti);
}

static void test_input_loss_and_capacity(void) {
    struct touch_flip flip;
    assert(touch_flip_configure(&flip, 1024, 600, "xy") == 0);
    struct touch_input *ti = touch_input_new(&flip);
    assert(ti != NULL);
    struct touch_event ev[8];

    struct touch_raw two[] = {
        R(EV_ABS, ABS_MT_TRACKING_ID, 9, 4.f),
        R(EV_ABS, ABS_MT_POSITION_X, 100, 4.f),
        R(EV_ABS, ABS_MT_POSITION_Y, 100, 4.f),
        R(EV_ABS, ABS_MT_SLOT, 1, 4.f),
        R(EV_ABS, ABS_MT_TRACKING_ID, 10, 4.f),
        R(EV_ABS, ABS_MT_POSITION_X, 300, 4.f),
        R(EV_ABS, ABS_MT_POSITION_Y, 300, 4.f),
        R(EV_SYN, SYN_REPORT, 0, 4.f),
        R(EV_SYN, SYN_DROPPED, 0, 4.1f),
        R(EV_ABS, ABS_MT_SLOT, 0, 4.1f),
        R(EV_ABS, ABS_MT_TRACKING_ID, 99, 4.1f),
        R(EV_SYN, SYN_REPORT, 0, 4.1f),
    };
    int n = touch_input_feed(ti, two, 12, ev, 1);
    assert(n == 1 && ev[0].kind == TOUCH_DOWN && ev[0].slot == 0);
    n = touch_input_feed(ti, NULL, 0, ev, 8);
    assert(n == 3);
    assert(ev[0].kind == TOUCH_DOWN && ev[0].slot == 1);
    assert(ev[1].kind == TOUCH_CANCEL && ev[1].slot == 0);
    assert(ev[2].kind == TOUCH_CANCEL && ev[2].slot == 1);

    struct touch_raw replacement[] = {
        R(EV_ABS, ABS_MT_SLOT, 0, 5.f),
        R(EV_ABS, ABS_MT_TRACKING_ID, 20, 5.f),
        R(EV_ABS, ABS_MT_POSITION_X, 30, 5.f),
        R(EV_ABS, ABS_MT_POSITION_Y, 40, 5.f),
        R(EV_SYN, SYN_REPORT, 0, 5.f),
        R(EV_ABS, ABS_MT_TRACKING_ID, 21, 5.1f),
        R(EV_ABS, ABS_MT_POSITION_X, 300, 5.1f),
        R(EV_ABS, ABS_MT_POSITION_Y, 400, 5.1f),
        R(EV_SYN, SYN_REPORT, 0, 5.1f),
    };
    n = touch_input_feed(ti, replacement, 9, ev, 8);
    assert(n == 3);
    assert(ev[0].kind == TOUCH_DOWN && ev[0].x == 30.f);
    assert(ev[1].kind == TOUCH_CANCEL && ev[1].x == 30.f);
    assert(ev[2].kind == TOUCH_DOWN && ev[2].x == 300.f);

    touch_input_set_flipped(ti, 1);
    n = touch_input_feed(ti, NULL, 0, ev, 8);
    assert(n == 1 && ev[0].kind == TOUCH_CANCEL);
    struct touch_raw after_flip[] = {
        R(EV_ABS, ABS_MT_TRACKING_ID, 22, 6.f),
        R(EV_ABS, ABS_MT_POSITION_X, 20, 6.f),
        R(EV_ABS, ABS_MT_POSITION_Y, 30, 6.f),
        R(EV_SYN, SYN_REPORT, 0, 6.f),
    };
    n = touch_input_feed(ti, after_flip, 4, ev, 8);
    assert(n == 1 && ev[0].kind == TOUCH_DOWN);
    assert(ev[0].x == 1004.f && ev[0].y == 570.f);

    struct touch_raw pending_up =
        R(EV_ABS, ABS_MT_TRACKING_ID, -1, 6.1f);
    assert(touch_input_feed(ti, &pending_up, 1, ev, 8) == 0);
    touch_input_set_flipped(ti, 0);
    n = touch_input_feed(ti, NULL, 0, ev, 8);
    assert(n == 1 && ev[0].kind == TOUCH_CANCEL);

    touch_input_free(ti);
}

static void test_pending_release_expires_as_cancel(void) {
    struct touch_flip flip;
    assert(touch_flip_configure(&flip, 1024, 600, "none") == 0);
    struct touch_input *ti = touch_input_new(&flip);
    assert(ti != NULL);
    struct touch_event ev[4];
    struct touch_raw down[] = {
        R(EV_ABS, ABS_MT_TRACKING_ID, 1, 1.f),
        R(EV_ABS, ABS_MT_POSITION_X, 10, 1.f),
        R(EV_ABS, ABS_MT_POSITION_Y, 20, 1.f),
        R(EV_SYN, SYN_REPORT, 0, 1.f),
    };
    assert(touch_input_feed(ti, down, 4, ev, 4) == 1);
    struct touch_raw pending_up =
        R(EV_ABS, ABS_MT_TRACKING_ID, -1, 1.1f);
    assert(touch_input_feed(ti, &pending_up, 1, ev, 4) == 0);
    assert(touch_input_expire(ti, 1.6001f, ev, 4) == 1);
    assert(ev[0].kind == TOUCH_CANCEL && ev[0].slot == 0);
    touch_input_free(ti);
}

static void collect_filtered_moves(double base, float result[2]) {
    struct touch_flip flip;
    assert(touch_flip_configure(&flip, 1024, 600, "none") == 0);
    struct touch_input *ti = touch_input_new(&flip);
    assert(ti != NULL);
    struct touch_event event[2];
    struct touch_raw down[] = {
        R(EV_ABS, ABS_MT_TRACKING_ID, 1, base),
        R(EV_ABS, ABS_MT_POSITION_X, 0, base),
        R(EV_ABS, ABS_MT_POSITION_Y, 0, base),
        R(EV_SYN, SYN_REPORT, 0, base),
    };
    assert(touch_input_feed(ti, down, 4, event, 2) == 1);
    for (int i = 0; i < 2; ++i) {
        double t = base + (double)(i + 1) * 0.1;
        struct touch_raw move[] = {
            R(EV_ABS, ABS_MT_POSITION_X, (i + 1) * 100, t),
            R(EV_SYN, SYN_REPORT, 0, t),
        };
        assert(touch_input_feed(ti, move, 2, event, 2) == 1);
        assert(event[0].kind == TOUCH_MOVE);
        result[i] = event[0].x;
    }
    touch_input_free(ti);
}

static void test_large_timestamp_precision(void) {
    const double base = 10000000.0;
    struct touch_flip flip;
    assert(touch_flip_configure(&flip, 1024, 600, "none") == 0);
    struct touch_input *ti = touch_input_new(&flip);
    assert(ti != NULL);
    struct touch_event event[2];
    struct touch_raw down[] = {
        R(EV_ABS, ABS_MT_TRACKING_ID, 1, base),
        R(EV_ABS, ABS_MT_POSITION_X, 10, base),
        R(EV_ABS, ABS_MT_POSITION_Y, 20, base),
        R(EV_SYN, SYN_REPORT, 0, base),
    };
    assert(touch_input_feed(ti, down, 4, event, 2) == 1);
    assert(touch_input_expire(ti, base + 0.5, event, 2) == 0);
    assert(touch_input_expire(ti, base + 0.500001, event, 2) == 1);
    assert(event[0].kind == TOUCH_CANCEL);
    touch_input_free(ti);

    float near_origin[2];
    float long_uptime[2];
    collect_filtered_moves(10.0, near_origin);
    collect_filtered_moves(base, long_uptime);
    assert(fabsf(near_origin[0] - long_uptime[0]) < 0.001f);
    assert(fabsf(near_origin[1] - long_uptime[1]) < 0.001f);
}

#ifdef __linux__
static void append_fake_event(unsigned short type, unsigned short code,
                              int value, long sec, long usec) {
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;
    event.time.tv_sec = sec;
    event.time.tv_usec = usec;
    memcpy(fake_input + fake_input_length, &event, sizeof(event));
    fake_input_length += sizeof(event);
}

static void test_linux_fd_uses_monotonic_clock(void) {
    struct touch_flip flip;
    assert(touch_flip_configure(&flip, 1024, 600, "none") == 0);
    struct touch_input *ti = touch_input_new(&flip);
    assert(ti != NULL);
    fake_input_length = 0;
    fake_input_offset = 0;
    fake_clock = -1;
    fake_clock_error = 1;
    append_fake_event(EV_ABS, ABS_MT_TRACKING_ID, 7, 432100, 250000);
    append_fake_event(EV_ABS, ABS_MT_POSITION_X, 20, 432100, 250000);
    append_fake_event(EV_ABS, ABS_MT_POSITION_Y, 30, 432100, 250000);
    append_fake_event(EV_SYN, SYN_REPORT, 0, 432100, 250000);

    struct touch_event event[2];
    assert(touch_input_read_fd(ti, 91, event, 2) == -1);
    assert(errno == EIO && fake_input_offset == 0);
    fake_clock_error = 0;
    assert(touch_input_read_fd(ti, 91, event, 2) == 1);
    assert(fake_clock == CLOCK_MONOTONIC);
    assert(event[0].kind == TOUCH_DOWN);
    assert(touch_input_read_fd(ti, 92, event, 2) == -1);
    assert(errno == EINVAL);
    assert(touch_input_expire(ti, 432100.8f, event, 2) == 1);
    assert(event[0].kind == TOUCH_CANCEL);
    touch_input_free(ti);
}
#endif

static void test_router_capture(void) {
    struct touch_router *r =
        touch_router_new(1024, 600, 48, 416, 56, 560, 400);
    assert(r != NULL);
    struct router_out out;

    struct touch_event open[] = {
        E(TOUCH_DOWN, 0, 900, 10, 5.f),
        E(TOUCH_MOVE, 0, 900, 40, 5.1f),
        E(TOUCH_MOVE, 0, 900, 80, 5.2f),
    };
    touch_router_feed(r, open, 3, &out);
    assert(out.open_panel == 1 && !out.cube.slot[0].active);

    touch_router_set_panel_open(r, 1);
    struct touch_event inside[] = {
        E(TOUCH_UP, 0, 900, 80, 5.3f),
        E(TOUCH_DOWN, 1, 800, 200, 6.f),
        E(TOUCH_UP, 1, 800, 200, 6.1f),
    };
    touch_router_feed(r, inside, 3, &out);
    assert(out.n_panel == 2 && !out.close_panel);
    assert(!out.cube.slot[1].active);

    struct touch_event outside[] = {
        E(TOUCH_DOWN, 0, 100, 400, 7.f),
        E(TOUCH_UP, 0, 100, 400, 7.1f),
    };
    touch_router_feed(r, outside, 2, &out);
    assert(out.close_panel == 1 && out.n_panel == 2);

    touch_router_set_panel_open(r, 0);
    struct touch_event drag[] = {
        E(TOUCH_DOWN, 0, 500, 300, 8.f),
        E(TOUCH_MOVE, 0, 520, 310, 8.1f),
    };
    touch_router_feed(r, drag, 2, &out);
    assert(out.cube.slot[0].active && out.cube.slot[0].x == 520);
    assert(!out.open_panel);
    struct touch_event drag_up = E(TOUCH_UP, 0, 520, 310, 8.2f);
    touch_router_feed(r, &drag_up, 1, &out);
    assert(!out.cube.slot[0].active);

    touch_router_set_panel_open(r, 1);
    struct touch_event fingers[] = {
        E(TOUCH_DOWN, 2, 700, 70, 9.f),
        E(TOUCH_DOWN, 3, 750, 200, 9.f),
        E(TOUCH_UP, 2, 700, 70, 9.1f),
        E(TOUCH_MOVE, 3, 760, 210, 9.2f),
        E(TOUCH_UP, 3, 760, 210, 9.3f),
    };
    touch_router_feed(r, fingers, 5, &out);
    assert(out.n_panel == 2);
    assert(out.panel[0].slot == 2 && out.panel[1].kind == TOUCH_UP);
    assert(!out.close_panel);

    struct touch_event handle[] = {
        E(TOUCH_DOWN, 4, 696, 66, 10.f),
        E(TOUCH_MOVE, 4, 696, 4, 10.1f),
    };
    touch_router_feed(r, handle, 2, &out);
    assert(out.close_panel == 1);
    touch_router_set_panel_open(r, 0);
    struct touch_event handle_up = E(TOUCH_UP, 4, 696, 4, 10.2f);
    touch_router_feed(r, &handle_up, 1, &out);
    assert(out.n_panel == 1 && out.panel[0].kind == TOUCH_UP);

    touch_router_free(r);
}

static void test_router_panel_queue(void) {
    struct touch_router *r =
        touch_router_new(1024, 600, 48, 416, 56, 560, 400);
    assert(r != NULL);
    touch_router_set_panel_open(r, 1);
    struct touch_event many[34];
    many[0] = E(TOUCH_DOWN, 0, 800, 200, 11.f);
    for (int i = 1; i < 33; ++i)
        many[i] = E(TOUCH_MOVE, 0, (float)(800 + i), 200, 11.f + i / 100.f);
    many[33] = E(TOUCH_UP, 0, 833, 200, 11.34f);

    struct router_out out;
    touch_router_feed(r, many, 34, &out);
    assert(out.n_panel == 32);
    touch_router_feed(r, NULL, 0, &out);
    assert(out.n_panel == 2);
    assert(out.panel[1].kind == TOUCH_UP);

    touch_router_reset(r);
    touch_router_feed(r, NULL, 0, &out);
    assert(out.n_panel == 0);
    for (int i = 0; i < TOUCH_MAX_SLOTS; ++i)
        assert(!out.cube.slot[i].active);
    touch_router_free(r);
}

static void test_router_opens_within_batch(void) {
    struct touch_router *r =
        touch_router_new(1024, 600, 48, 416, 56, 560, 400);
    assert(r != NULL);
    struct touch_event batch[] = {
        E(TOUCH_DOWN, 0, 900, 10, 12.f),
        E(TOUCH_MOVE, 0, 900, 80, 12.1f),
        E(TOUCH_DOWN, 1, 800, 200, 12.1f),
        E(TOUCH_MOVE, 1, 810, 220, 12.2f),
    };
    struct router_out out;
    touch_router_feed(r, batch, 4, &out);
    assert(out.open_panel == 1);
    assert(out.n_panel == 0);
    assert(!out.cube.slot[1].active);

    struct touch_event ignored[] = {
        E(TOUCH_UP, 1, 800, 200, 12.2f),
        E(TOUCH_DOWN, 2, 800, 200, 12.2f),
        E(TOUCH_UP, 2, 800, 200, 12.3f),
    };
    touch_router_feed(r, ignored, 3, &out);
    assert(out.n_panel == 0);
    assert(!out.cube.slot[2].active);

    struct touch_event opening_up = E(TOUCH_UP, 0, 900, 80, 12.3f);
    touch_router_feed(r, &opening_up, 1, &out);
    assert(out.n_panel == 0);
    touch_router_free(r);
}

static void test_slot_selection_survives_flip_and_cancel(void) {
    /* ABS_MT_SLOT is only sent when the kernel's selection changes, so a
     * flip or a logical cancel must not forget it: the next contact reported
     * without a slot still belongs to slot 1. */
    struct touch_flip flip;
    assert(touch_flip_configure(&flip, 1024, 600, "xy") == 0);
    struct touch_input *ti = touch_input_new(&flip);
    assert(ti != NULL);
    struct touch_event ev[32];
    struct touch_raw select_one[] = {
        R(EV_ABS, ABS_MT_SLOT, 1, 20.f), R(EV_ABS, ABS_MT_TRACKING_ID, 30, 20.f),
        R(EV_ABS, ABS_MT_POSITION_X, 100, 20.f), R(EV_ABS, ABS_MT_POSITION_Y, 100, 20.f),
        R(EV_SYN, SYN_REPORT, 0, 20.f) };
    int n = touch_input_feed(ti, select_one, 5, ev, 32);
    assert(n == 1 && ev[0].slot == 1);
    touch_input_set_flipped(ti, 1);
    touch_input_cancel_all(ti, ev, 32);
    struct touch_raw again[] = {
        R(EV_ABS, ABS_MT_TRACKING_ID, 31, 21.f), R(EV_ABS, ABS_MT_POSITION_X, 120, 21.f),
        R(EV_ABS, ABS_MT_POSITION_Y, 120, 21.f), R(EV_SYN, SYN_REPORT, 0, 21.f) };
    n = touch_input_feed(ti, again, 4, ev, 32);
    assert(n == 1 && ev[0].kind == TOUCH_DOWN && ev[0].slot == 1);
    touch_input_free(ti);
}

int main(void) {
    test_input_frames();
    test_input_loss_and_capacity();
    test_pending_release_expires_as_cancel();
    test_large_timestamp_precision();
#ifdef __linux__
    test_linux_fd_uses_monotonic_clock();
#endif
    test_router_capture();
    test_router_panel_queue();
    test_router_opens_within_batch();
    test_slot_selection_survives_flip_and_cancel();
    puts("touch ok");
    return 0;
}
