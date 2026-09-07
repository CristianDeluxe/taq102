#include "touch_input.h"

#include "oneeuro.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>

#ifdef __linux__
#include <linux/input.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

struct touch_input_linux_io {
    ssize_t (*read_event)(int fd, void *buffer, size_t length);
    int (*ioctl_event)(int fd, unsigned long request, void *argument);
};

static ssize_t read_event(int fd, void *buffer, size_t length) {
    return read(fd, buffer, length);
}

static int ioctl_event(int fd, unsigned long request, void *argument) {
    return ioctl(fd, request, argument);
}

__attribute__((weak)) const struct touch_input_linux_io touch_input_linux_io = {
    read_event,
    ioctl_event,
};
#endif

#define FILTER_MIN_CUTOFF 1.0f
#define FILTER_BETA 0.02f
#define SLOT_SILENCE_SECONDS 0.5

struct touch_slot {
    int active;
    int announced;
    int tracking_id;
    int frame_event;
    float x;
    float y;
    double last_spoke;
    double filter_time_origin;
    int filter_time_started;
    struct oneeuro fx;
    struct oneeuro fy;
};

struct event_buffer {
    struct touch_event *event;
    size_t length;
    size_t capacity;
};

struct touch_input {
    struct touch_flip flip;
    int flipped;
    int current_slot;
    int discarding;
    struct touch_slot slot[TOUCH_MAX_SLOTS];
    struct event_buffer frame;
    struct event_buffer ready;
#ifdef __linux__
    unsigned char partial[sizeof(struct input_event)];
    size_t partial_length;
    int resync_needed;
    int resync_errno;
    double resync_time;
    int input_fd;
#endif
};

static void reset_filter(struct touch_slot *slot) {
    oneeuro_init(&slot->fx, FILTER_MIN_CUTOFF, FILTER_BETA);
    oneeuro_init(&slot->fy, FILTER_MIN_CUTOFF, FILTER_BETA);
}

static int buffer_reserve(struct event_buffer *buffer, size_t needed) {
    if (needed <= buffer->capacity) return 0;
    size_t capacity = buffer->capacity ? buffer->capacity : 16;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            errno = ENOMEM;
            return -1;
        }
        capacity *= 2;
    }
    struct touch_event *event = realloc(buffer->event, capacity * sizeof(*event));
    if (!event) return -1;
    buffer->event = event;
    buffer->capacity = capacity;
    return 0;
}

static int buffer_append(struct event_buffer *buffer,
                         const struct touch_event *event) {
    if (buffer_reserve(buffer, buffer->length + 1) < 0) return -1;
    buffer->event[buffer->length++] = *event;
    return 0;
}

static int append_frame_event(struct touch_input *ti, enum touch_kind kind,
                              int slot_index, double t) {
    struct touch_slot *slot = &ti->slot[slot_index];
    struct touch_event event = {kind, slot_index, slot->x, slot->y, t};
    if (buffer_append(&ti->frame, &event) < 0) return -1;
    slot->frame_event = (int)ti->frame.length - 1;
    return 0;
}

static int publish_frame(struct touch_input *ti) {
    if (ti->frame.length == 0) return 0;
    if (buffer_reserve(&ti->ready, ti->ready.length + ti->frame.length) < 0)
        return -1;
    memcpy(ti->ready.event + ti->ready.length, ti->frame.event,
           ti->frame.length * sizeof(*ti->frame.event));
    ti->ready.length += ti->frame.length;
    for (size_t i = 0; i < ti->frame.length; ++i) {
        const struct touch_event *event = &ti->frame.event[i];
        if (event->kind == TOUCH_DOWN)
            ti->slot[event->slot].announced = 1;
        else if (event->kind == TOUCH_UP || event->kind == TOUCH_CANCEL)
            ti->slot[event->slot].announced = 0;
    }
    ti->frame.length = 0;
    for (int i = 0; i < TOUCH_MAX_SLOTS; ++i) {
        ti->slot[i].frame_event = -1;
    }
    return 0;
}

static void discard_frame_slot(struct touch_input *ti, int slot_index) {
    size_t write_index = 0;
    for (size_t i = 0; i < ti->frame.length; ++i) {
        if (ti->frame.event[i].slot != slot_index)
            ti->frame.event[write_index++] = ti->frame.event[i];
    }
    ti->frame.length = write_index;
    for (int i = 0; i < TOUCH_MAX_SLOTS; ++i) ti->slot[i].frame_event = -1;
    for (size_t i = 0; i < ti->frame.length; ++i)
        ti->slot[ti->frame.event[i].slot].frame_event = (int)i;
}

static int drain_ready(struct touch_input *ti, struct touch_event *out, int max) {
    if (max < 0 || (max > 0 && !out)) {
        errno = EINVAL;
        return -1;
    }
    size_t count = ti->ready.length;
    if (count > (size_t)max) count = (size_t)max;
    if (count > 0) {
        memcpy(out, ti->ready.event, count * sizeof(*out));
        ti->ready.length -= count;
        memmove(ti->ready.event, ti->ready.event + count,
                ti->ready.length * sizeof(*ti->ready.event));
    }
    return (int)count;
}

static void clear_slot(struct touch_slot *slot) {
    slot->active = 0;
    slot->announced = 0;
    slot->tracking_id = -1;
    slot->frame_event = -1;
    slot->filter_time_origin = 0.0;
    slot->filter_time_started = 0;
    reset_filter(slot);
}

static int cancel_active_to_frame(struct touch_input *ti, double t) {
    ti->frame.length = 0;
    for (int i = 0; i < TOUCH_MAX_SLOTS; ++i) {
        struct touch_slot *slot = &ti->slot[i];
        slot->frame_event = -1;
        if (slot->announced &&
            append_frame_event(ti, TOUCH_CANCEL, i, t) < 0)
            return -1;
        clear_slot(slot);
    }
    return 0;
}

static int cancel_active_to_ready(struct touch_input *ti, double t) {
    ti->frame.length = 0;
    for (int i = 0; i < TOUCH_MAX_SLOTS; ++i) {
        struct touch_slot *slot = &ti->slot[i];
        if (slot->announced) {
            struct touch_event event = {TOUCH_CANCEL, i, slot->x, slot->y, t};
            if (buffer_append(&ti->ready, &event) < 0) return -1;
        }
        clear_slot(slot);
    }
    return 0;
}

struct touch_input *touch_input_new(const struct touch_flip *flip) {
    if (!flip) {
        errno = EINVAL;
        return NULL;
    }
    struct touch_input *ti = calloc(1, sizeof(*ti));
    if (!ti) return NULL;
    ti->flip = *flip;
    ti->current_slot = 0;
#ifdef __linux__
    ti->input_fd = -1;
#endif
    for (int i = 0; i < TOUCH_MAX_SLOTS; ++i) clear_slot(&ti->slot[i]);
    return ti;
}

void touch_input_free(struct touch_input *ti) {
    if (!ti) return;
    free(ti->frame.event);
    free(ti->ready.event);
    free(ti);
}

void touch_input_set_flipped(struct touch_input *ti, int flipped) {
    if (!ti) return;
    flipped = !!flipped;
    if (ti->flipped == flipped) return;
    double t = 0.0;
    for (int i = 0; i < TOUCH_MAX_SLOTS; ++i)
        if (ti->slot[i].last_spoke > t) t = ti->slot[i].last_spoke;
    if (cancel_active_to_ready(ti, t) < 0) return;
    ti->discarding = 0;
    /* The kernel's slot selection survives our logical cancel: ABS_MT_SLOT
     * is only sent when it changes, so forgetting it here would put the
     * next contact in slot 0 while the driver keeps reporting slot 1. */
    ti->flipped = flipped;
}

static int handle_tracking_id(struct touch_input *ti, int value, double t) {
    int index = ti->current_slot;
    if (index < 0 || index >= TOUCH_MAX_SLOTS) return 0;
    struct touch_slot *slot = &ti->slot[index];
    slot->last_spoke = t;
    if (value == -1) {
        if (!slot->active) return 0;
        if (append_frame_event(ti, TOUCH_UP, index, t) < 0) return -1;
        slot->active = 0;
        slot->tracking_id = -1;
        return 0;
    }
    if (slot->active && slot->tracking_id == value) return 0;
    if (slot->active && append_frame_event(ti, TOUCH_CANCEL, index, t) < 0)
        return -1;
    slot->active = 1;
    slot->tracking_id = value;
    reset_filter(slot);
    slot->filter_time_origin = t;
    slot->filter_time_started = 1;
    return append_frame_event(ti, TOUCH_DOWN, index, t);
}

static int handle_position(struct touch_input *ti, unsigned short code,
                           int value, double t) {
    int index = ti->current_slot;
    if (index < 0 || index >= TOUCH_MAX_SLOTS) return 0;
    struct touch_slot *slot = &ti->slot[index];
    slot->last_spoke = t;
    if (!slot->active) return 0;
    if (!slot->filter_time_started) {
        slot->filter_time_origin = t;
        slot->filter_time_started = 1;
    }
    float filter_time = (float)(t - slot->filter_time_origin);
    if (code == ABS_MT_POSITION_X) {
        float x = (float)touch_flip_value(&ti->flip, TOUCH_AXIS_X, value,
                                          ti->flipped);
        slot->x = oneeuro_apply(&slot->fx, x, filter_time);
    } else {
        float y = (float)touch_flip_value(&ti->flip, TOUCH_AXIS_Y, value,
                                          ti->flipped);
        slot->y = oneeuro_apply(&slot->fy, y, filter_time);
    }
    int event_index = slot->frame_event;
    if (event_index >= 0 &&
        (ti->frame.event[event_index].kind == TOUCH_DOWN ||
         ti->frame.event[event_index].kind == TOUCH_MOVE)) {
        ti->frame.event[event_index].x = slot->x;
        ti->frame.event[event_index].y = slot->y;
        ti->frame.event[event_index].t = t;
        return 0;
    }
    return append_frame_event(ti, TOUCH_MOVE, index, t);
}

static int feed_one(struct touch_input *ti, const struct touch_raw *raw) {
    if (raw->type == EV_SYN && raw->code == SYN_DROPPED) {
        if (cancel_active_to_frame(ti, raw->t) < 0) return -1;
        ti->discarding = 1;
        ti->current_slot = 0;
        return 0;
    }
    if (ti->discarding) {
        if (raw->type == EV_SYN && raw->code == SYN_REPORT) {
            ti->discarding = 0;
            return publish_frame(ti);
        }
        return 0;
    }
    if (raw->type == EV_SYN && raw->code == SYN_REPORT)
        return publish_frame(ti);
    if (raw->type != EV_ABS) return 0;
    if (raw->code == ABS_MT_SLOT) {
        ti->current_slot = raw->value;
        return 0;
    }
    if (raw->code == ABS_MT_TRACKING_ID)
        return handle_tracking_id(ti, raw->value, raw->t);
    if (raw->code == ABS_MT_POSITION_X || raw->code == ABS_MT_POSITION_Y)
        return handle_position(ti, raw->code, raw->value, raw->t);
    return 0;
}

int touch_input_feed(struct touch_input *ti, const struct touch_raw *raw, int n,
                     struct touch_event *out, int max) {
    if (!ti || n < 0 || (n > 0 && !raw)) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < n; ++i)
        if (feed_one(ti, &raw[i]) < 0) return -1;
    return drain_ready(ti, out, max);
}

int touch_input_expire(struct touch_input *ti, double now,
                       struct touch_event *out, int max) {
    if (!ti) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < TOUCH_MAX_SLOTS; ++i) {
        struct touch_slot *slot = &ti->slot[i];
        if ((!slot->active && !slot->announced) ||
            now - slot->last_spoke <= SLOT_SILENCE_SECONDS)
            continue;
        discard_frame_slot(ti, i);
        if (slot->announced) {
            struct touch_event event = {TOUCH_CANCEL, i, slot->x, slot->y, now};
            if (buffer_append(&ti->ready, &event) < 0) return -1;
        }
        clear_slot(slot);
    }
    return drain_ready(ti, out, max);
}

int touch_input_cancel_all(struct touch_input *ti, struct touch_event *out,
                           int max) {
    if (!ti) {
        errno = EINVAL;
        return -1;
    }
    double t = 0.0;
    for (int i = 0; i < TOUCH_MAX_SLOTS; ++i)
        if (ti->slot[i].last_spoke > t) t = ti->slot[i].last_spoke;
    if (cancel_active_to_ready(ti, t) < 0) return -1;
    ti->discarding = 0;
    return drain_ready(ti, out, max);
}

#ifdef __linux__
static int resync_code(int fd, unsigned code, int32_t *values, int slots) {
    values[0] = (int32_t)code;
    return touch_input_linux_io.ioctl_event(
        fd, EVIOCGMTSLOTS((slots + 1) * sizeof(*values)), values);
}

static int resync_driver(struct touch_input *ti, int fd, double t) {
    int32_t ids[TOUCH_MAX_SLOTS + 1];
    int32_t xs[TOUCH_MAX_SLOTS + 1];
    int32_t ys[TOUCH_MAX_SLOTS + 1];
    struct input_absinfo slot_info;
    if (touch_input_linux_io.ioctl_event(
            fd, EVIOCGABS(ABS_MT_SLOT), &slot_info) < 0)
        return -1;
    int first = slot_info.minimum;
    int count = slot_info.maximum - first + 1;
    if (count <= 0) {
        errno = EPROTO;
        return -1;
    }
    if (count > TOUCH_MAX_SLOTS) count = TOUCH_MAX_SLOTS;
    for (int i = 0; i <= TOUCH_MAX_SLOTS; ++i) {
        ids[i] = -1;
        xs[i] = 0;
        ys[i] = 0;
    }
    if (resync_code(fd, ABS_MT_TRACKING_ID, ids, count) < 0)
        return -1;
    if (resync_code(fd, ABS_MT_POSITION_X, xs, count) < 0 ||
        resync_code(fd, ABS_MT_POSITION_Y, ys, count) < 0)
        return -1;
    ti->current_slot = slot_info.value;
    for (int i = 0; i < count; ++i) {
        if (ids[i + 1] < 0) continue;
        int slot_index = first + i;
        if (slot_index < 0 || slot_index >= TOUCH_MAX_SLOTS) continue;
        struct touch_slot *slot = &ti->slot[slot_index];
        clear_slot(slot);
        slot->active = 1;
        slot->announced = 1;
        slot->tracking_id = ids[i + 1];
        slot->last_spoke = t;
        slot->filter_time_origin = t;
        slot->filter_time_started = 1;
        slot->x = oneeuro_apply(
            &slot->fx,
            (float)touch_flip_value(&ti->flip, TOUCH_AXIS_X, xs[i + 1],
                                    ti->flipped),
            0.f);
        slot->y = oneeuro_apply(
            &slot->fy,
            (float)touch_flip_value(&ti->flip, TOUCH_AXIS_Y, ys[i + 1],
                                    ti->flipped),
            0.f);
        struct touch_event down = {
            TOUCH_DOWN, slot_index, slot->x, slot->y, t};
        if (buffer_append(&ti->ready, &down) < 0) return -1;
    }
    return 0;
}

static int process_input_event(struct touch_input *ti, int fd,
                               const struct input_event *event) {
    if (ti->resync_needed) {
        if (resync_driver(ti, fd, ti->resync_time) < 0) {
            ti->resync_errno = errno ? errno : EIO;
            return 0;
        }
        ti->resync_needed = 0;
        ti->resync_errno = 0;
    }
    struct touch_raw raw = {
        event->type,
        event->code,
        event->value,
        (double)event->time.tv_sec + (double)event->time.tv_usec * 1e-6,
    };
    int was_discarding = ti->discarding;
    if (feed_one(ti, &raw) < 0) return -1;
    if (was_discarding && !ti->discarding && event->type == EV_SYN &&
        event->code == SYN_REPORT) {
        ti->resync_needed = 1;
        ti->resync_time = raw.t;
    }
    if (ti->resync_needed) {
        if (resync_driver(ti, fd, ti->resync_time) == 0) {
            ti->resync_needed = 0;
            ti->resync_errno = 0;
        } else {
            ti->resync_errno = errno ? errno : EIO;
        }
    }
    return 0;
}
#endif

int touch_input_read_fd(struct touch_input *ti, int fd, struct touch_event *out,
                        int max) {
#ifdef __linux__
    if (!ti || fd < 0) {
        errno = EINVAL;
        return -1;
    }
    if (ti->input_fd >= 0 && ti->input_fd != fd) {
        errno = EINVAL;
        return -1;
    }
    if (ti->input_fd < 0) {
        int clock_id = CLOCK_MONOTONIC;
        if (touch_input_linux_io.ioctl_event(
                fd, EVIOCSCLOCKID, &clock_id) < 0)
            return -1;
        ti->input_fd = fd;
    }
    if (ti->resync_needed && resync_driver(ti, fd, ti->resync_time) == 0) {
        ti->resync_needed = 0;
        ti->resync_errno = 0;
    }
    for (;;) {
        ssize_t got = touch_input_linux_io.read_event(
            fd, ti->partial + ti->partial_length,
            sizeof(ti->partial) - ti->partial_length);
        if (got > 0) {
            ti->partial_length += (size_t)got;
            if (ti->partial_length == sizeof(struct input_event)) {
                struct input_event event;
                memcpy(&event, ti->partial, sizeof(event));
                ti->partial_length = 0;
                if (process_input_event(ti, fd, &event) < 0) return -1;
            }
            continue;
        }
        if (got == 0 || errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        return -1;
    }
    int drained = drain_ready(ti, out, max);
    if (drained != 0 || !ti->resync_needed) return drained;
    errno = ti->resync_errno ? ti->resync_errno : EIO;
    return -1;
#else
    (void)ti;
    (void)fd;
    (void)out;
    (void)max;
    errno = ENOTSUP;
    return -1;
#endif
}
