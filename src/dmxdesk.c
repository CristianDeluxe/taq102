// The lighting desk: the tablet as a control surface for the QLC+ show
// running on the Mac. It draws first, then dials: the session fetches the
// console the master has loaded, the show map is checked against it, the
// tiles are drawn, and one message goes out per gesture. What lights up is
// what the master says is running.
//
//   dmxdesk --host 192.168.1.50 --map /etc/taq102/show-map.json
//
// DMXDESK_DUMP=<file.ppm> writes the first frame and exits, so the screen can
// be checked from a laptop without a camera. SIGUSR1 writes the same file
// without stopping, which is how a running desk is photographed.
// DMXDESK_FLIP=1 repaints and flips on every loop, whether or not anything
// changed: a diagnostic for telling a fault in the flip path from one in the
// picture. The desk prints how many frames it flipped every ten seconds, so
// "it is static" is a number rather than an assumption.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "canvas.h"
#include "desk_layout.h"
#include "desk_model.h"
#include "desk_paint.h"
#include "desk_present_drm.h"
#include "font.h"
#include "qlc_codec.h"
#include "qlc_session.h"
#include "showmap.h"
#include "showmap_validate.h"
#include "status.h"
#include "statusbar.h"
#include "touch_flip.h"
#include "touch_input.h"
#include "vcjson.h"

#define VC_LIMIT (1024 * 1024)
// The master pushes only when something changes, and its own ping is every
// five seconds, so the desk asks a question of its own well inside the window
// it treats as stale.
#define HEARTBEAT_MS 400
#define STALE_MS 750
#define RECONNECT_MS 1500
#define CONNECT_TIMEOUT_MS 3000
#define FETCH_TIMEOUT_MS 4000
#define STATUS_MS 1000

static volatile sig_atomic_t stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

static volatile sig_atomic_t snapshot;
static void on_snapshot(int sig) { (void)sig; snapshot = 1; }

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void dump_ppm(const struct canvas *c, const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        perror("DMXDESK_DUMP");
        return;
    }
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        unsigned char rgb[3] = { (p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void apply_frame(struct desk_model *model, const char *frame) {
    struct qlc_msg msg;
    if (qlc_decode(frame, strlen(frame), &msg) != 0)
        return;
    switch (msg.kind) {
    case QLC_FUNCTION:
        desk_apply_function(model, msg.function_id, msg.running);
        break;
    case QLC_GRAND_MASTER:
        desk_apply_master(model, msg.value);
        break;
    default:
        break;
    }
}

// One gesture, one frame. A frame refused because the link went down between
// the touch and the send is dropped, never kept: a toggle sent late is a
// second toggle.
static void send_action(struct qlc_session *session, struct desk_action action) {
    char frame[64];
    int n = -1;
    switch (action.kind) {
    case DESK_ACT_TOGGLE:
        n = qlc_encode_toggle(frame, sizeof frame, action.widget_id);
        break;
    case DESK_ACT_MASTER:
        n = qlc_encode_grand_master(frame, sizeof frame, action.value);
        break;
    case DESK_ACT_NONE:
        return;
    }
    if (n > 0)
        qlc_session_send(session, frame);
}

static enum desk_link desk_link_of(enum qlc_link link) {
    switch (link) {
    case QLC_READY:      return DESK_LINK_READY;
    case QLC_FETCHING:   return DESK_LINK_SYNCING;
    case QLC_CONNECTING: return DESK_LINK_CONNECTING;
    default:             return DESK_LINK_DOWN;
    }
}

// The bar's status is read once a second; only a change repaints.
static int status_changed(const struct status *a, const struct status *b) {
    return a->have_batt != b->have_batt || a->cap != b->cap || a->plugged != b->plugged ||
           status_wifi_bars(a) != status_wifi_bars(b);
}

int main(int argc, char **argv) {
    const char *host = NULL;
    int port = 9999;
    const char *map_path = "/etc/taq102/show-map.json";
    const char *card = "/dev/dri/card0";
    const char *touch_device = "/dev/input/event1";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) map_path = argv[++i];
        else if (!strcmp(argv[i], "--card") && i + 1 < argc) card = argv[++i];
        else if (!strcmp(argv[i], "--touch") && i + 1 < argc) touch_device = argv[++i];
        else {
            fprintf(stderr, "usage: %s --host H [--port P] [--map FILE]"
                            " [--card /dev/dri/cardN] [--touch /dev/input/eventN]\n",
                    argv[0]);
            return 2;
        }
    }
    if (!host || port <= 0 || port > 65535) {
        fprintf(stderr, "dmxdesk: --host is required until the tablet keeps one of its own\n");
        return 2;
    }

    struct show_map map;
    if (showmap_load(map_path, &map) != 0)
        return 1;

    // The display first. The model is built against an empty console, so
    // every tile is drawn disabled with its reason before the network is
    // touched; the session's first snapshot rebuilds it.
    struct present *present = present_open(card);
    if (!present)
        return 1;
    int w = present_width(present), h = present_height(present);
    struct canvas canvas = { calloc((size_t)w * h, 4), w, h };
    if (!canvas.px) {
        present_close(present);
        return 1;
    }
    struct desk_fonts fonts = {
        font_open("/usr/share/fonts/taq102/Inter-SemiBold.ttf", 22),
        font_open("/usr/share/fonts/taq102/Inter-SemiBold.ttf", 56),
        font_open("/usr/share/fonts/taq102/Inter-Regular.ttf", 20),
    };
    const struct statusbar_style bar_style = {
        DESK_GLASS, DESK_GLASS, DESK_INK, DESK_MUTED, DESK_INK, DESK_AMBER, DESK_WARN,
        "/usr/share/fonts/taq102/Inter-SemiBold.ttf",
    };

    struct vc_doc console;
    memset(&console, 0, sizeof console);
    struct desk_model model;
    int enabled = showmap_build(&model, &map, &console);
    printf("desk: %s, %d of %d controls enabled before the first snapshot\n",
           map.key, enabled, map.count);

    // The controller reports in its own units on the mainline driver and in
    // screen pixels on the vendor one, so its declared maxima decide the
    // scale rather than a constant.
    int touch_fd = open(touch_device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct touch_input *touch = NULL;
    struct touch_flip flip;
    if (touch_fd >= 0) {
        int declared_x = 0, declared_y = 0;
        struct input_absinfo ai;
        if (ioctl(touch_fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0)
            declared_x = ai.maximum;
        if (ioctl(touch_fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0)
            declared_y = ai.maximum;
        touch_flip_configure(&flip, w, h, declared_x, declared_y,
                             getenv("DMXDESK_TOUCH_FLIP"));
        touch = touch_input_new(&flip);
        printf("touch: declared %dx%d, mapping %dx%d, %s\n", declared_x,
               declared_y, w, h, flip.name);
    }
    if (!touch)
        fprintf(stderr, "no touch on %s: the desk will only show\n", touch_device);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGUSR1, on_snapshot);

    struct qlc_session_config cfg = {
        .port = port, .heartbeat_ms = HEARTBEAT_MS, .stale_ms = STALE_MS,
        .reconnect_ms = RECONNECT_MS, .connect_timeout_ms = CONNECT_TIMEOUT_MS,
        .fetch_timeout_ms = FETCH_TIMEOUT_MS, .snapshot_limit = VC_LIMIT,
    };
    snprintf(cfg.host, sizeof cfg.host, "%s", host);
    struct qlc_session *session = qlc_session_new(&cfg);
    if (!session)
        return 1;

    const char *dump = getenv("DMXDESK_DUMP");
    // DMXDESK_RTT=1 prints every heartbeat's round trip: the raw material for
    // deciding whether 750 ms of silence is a dead master or a slow Wi-Fi.
    int log_rtt = getenv("DMXDESK_RTT") != NULL;
    int last_rtt_logged = -1;
    int force_flip = getenv("DMXDESK_FLIP") != NULL;
    unsigned long flips = 0;
    int64_t last_report_ms = now_ms();
    int64_t last_status_ms = 0;
    struct status status;
    memset(&status, 0, sizeof status);
    enum qlc_link last_link = QLC_DOWN;
    char last_reason[96] = "";

    while (!stop) {
        struct pollfd fds[3];
        int count = 0, touch_slot = -1;
        if (touch) {
            touch_slot = count;
            fds[count].fd = touch_fd;
            fds[count].events = POLLIN;
            fds[count].revents = 0;
            count++;
        }
        count += qlc_session_pollfds(session, fds + count, 2);
        // Forced flips run at the panel's own pace, not the loop's.
        poll(fds, (nfds_t)count, force_flip ? 0 : 100);
        int64_t now = now_ms();

        if (touch_slot >= 0 && (fds[touch_slot].revents & POLLIN)) {
            struct touch_event events[32];
            int n = touch_input_read_fd(touch, touch_fd, events, 32);
            for (int i = 0; i < n; i++) {
                struct desk_action action = { DESK_ACT_NONE, -1, 0 };
                switch (events[i].kind) {
                case TOUCH_DOWN:
                    action = desk_touch_down(&model, events[i].slot,
                                             (int)events[i].x, (int)events[i].y);
                    break;
                case TOUCH_MOVE:
                    action = desk_touch_move(&model, events[i].slot,
                                             (int)events[i].x, (int)events[i].y);
                    break;
                case TOUCH_UP:
                    action = desk_touch_up(&model, events[i].slot,
                                           (int)events[i].x, (int)events[i].y);
                    break;
                case TOUCH_CANCEL:
                    desk_touch_cancel(&model, events[i].slot);
                    break;
                }
                send_action(session, action);
            }
        }

        enum qlc_link link = qlc_session_step(session, now);
        struct vc_doc fresh;
        if (qlc_session_take_snapshot(session, &fresh)) {
            vc_free(&console);
            console = fresh;
            enabled = showmap_build(&model, &map, &console);
            printf("desk: %d of %d controls enabled against the master's console\n",
                   enabled, map.count);
        }
        char frame[4096];
        while (qlc_session_recv(session, frame, sizeof frame) == 1)
            apply_frame(&model, frame);
        desk_set_link(&model, desk_link_of(link));
        if (log_rtt && link == QLC_READY && qlc_session_last_rtt(session) != last_rtt_logged) {
            last_rtt_logged = qlc_session_last_rtt(session);
            if (last_rtt_logged >= 0)
                printf("rtt %d\n", last_rtt_logged);
        }
        if (link != last_link || strcmp(last_reason, qlc_session_reason(session)) != 0) {
            last_link = link;
            snprintf(last_reason, sizeof last_reason, "%s", qlc_session_reason(session));
            fprintf(stderr, "desk: link %s (%s)\n",
                    link == QLC_READY ? "ready" : link == QLC_FETCHING ? "reading the show"
                    : link == QLC_CONNECTING ? "connecting" : "down", last_reason);
        }

        if (now - last_status_ms >= STATUS_MS) {
            last_status_ms = now;
            struct status next;
            status_read(&next);
            if (status_changed(&status, &next))
                model.dirty = 1;
            status = next;
        }

        if (model.dirty || force_flip) {
            // A forced flip re-presents the same canvas: the point is the
            // flip, not the paint, and a full repaint on this CPU would cap
            // the rate far below the panel's.
            if (model.dirty) {
                desk_paint(&canvas, &model, &fonts);
                statusbar_paint(&canvas, &status, &bar_style);
                model.dirty = 0;
            }
            if (present_frame(present, &canvas) != 0)
                break;
            flips++;
            if (dump) {
                dump_ppm(&canvas, dump);
                break;
            }
        }
        if (now - last_report_ms >= 10000) {
            last_report_ms = now;
            printf("desk: %lu flips so far, link %s (%s)\n", flips,
                   link == QLC_READY ? "up" : "down", qlc_session_reason(session));
            fflush(stdout);
        }
        if (snapshot) {
            snapshot = 0;
            dump_ppm(&canvas, "/tmp/desk.ppm");
            printf("desk: wrote /tmp/desk.ppm\n");
            fflush(stdout);
        }
    }

    if (touch)
        touch_input_free(touch);
    if (touch_fd >= 0)
        close(touch_fd);
    qlc_session_free(session);
    font_close(fonts.tile);
    font_close(fonts.value);
    font_close(fonts.label);
    free(canvas.px);
    present_close(present);
    vc_free(&console);
    return 0;
}
