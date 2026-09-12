// The lighting desk: the tablet as a control surface for the QLC+ show
// running on the Mac. It fetches the console the master has loaded, checks
// the show map against it, draws the tiles, and sends one message per
// gesture. What lights up is what the master says is running.
//
//   dmxdesk --host 192.168.1.50 --map /etc/taq102/deluxe-eventos.json
//
// DMXDESK_DUMP=<file.ppm> writes the first frame and exits, so the screen can
// be checked from a laptop without a camera.
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
#include "http_get.h"
#include "qlc_codec.h"
#include "showmap.h"
#include "showmap_validate.h"
#include "touch_flip.h"
#include "touch_input.h"
#include "vcjson.h"
#include "ws_client.h"

#define VC_LIMIT (1024 * 1024)
// The master pushes only when something changes, and its own ping is every
// five seconds, so the desk asks a question of its own well inside the window
// it treats as stale.
#define HEARTBEAT_MS 400
#define STALE_MS 750
#define RECONNECT_MS 1500

static volatile sig_atomic_t stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

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

// One connection's worth of state. A generation number is not needed while the
// desk holds exactly one socket and drops everything on reconnect.
struct link {
    struct ws *ws;
    int64_t last_heard_ms;
    int64_t last_beat_ms;
    int64_t next_try_ms;
};

static int fetch_console(const char *host, int port, struct vc_doc *doc) {
    char *body = NULL;
    size_t len = 0;
    if (http_get(host, port, "/vc.json", 4000, VC_LIMIT, &body, &len) != 0)
        return -1;
    int rc = vc_parse(body, len, doc);
    free(body);
    if (rc != 0)
        fprintf(stderr, "the console at %s is not one this desk can read\n", host);
    return rc;
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

static void send_action(struct link *link, struct desk_action action) {
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
    if (n < 0 || !link->ws)
        return;
    if (ws_send_text(link->ws, frame) != 0) {
        ws_close(link->ws);
        link->ws = NULL;
    }
}

int main(int argc, char **argv) {
    const char *host = "192.168.1.50";
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
            fprintf(stderr, "usage: %s [--host H] [--port P] [--map FILE]"
                            " [--card /dev/dri/cardN] [--touch /dev/input/eventN]\n",
                    argv[0]);
            return 2;
        }
    }

    struct show_map map;
    if (showmap_load(map_path, &map) != 0)
        return 1;

    // A first read before the display comes up, so a map that does not match
    // the show is reported on the terminal that started it rather than only
    // on the tablet's own screen. Every later connection reads it again.
    struct vc_doc console;
    memset(&console, 0, sizeof console);
    fetch_console(host, port, &console);

    struct desk_model model;
    int enabled = showmap_build(&model, &map, &console);
    printf("desk: %s, %d of %d controls enabled\n", map.key, enabled, map.count);

    struct present *present = present_open(card);
    if (!present) {
        vc_free(&console);
        return 1;
    }
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

    struct link link = { NULL, 0, 0, 0 };
    const char *dump = getenv("DMXDESK_DUMP");

    while (!stop) {
        int64_t now = now_ms();

        if (!link.ws && now >= link.next_try_ms) {
            desk_set_link(&model, DESK_LINK_CONNECTING);
            link.ws = ws_connect(host, port, "/qlcplusWS", 3000);
            link.next_try_ms = now + RECONNECT_MS;
            if (link.ws) {
                link.last_heard_ms = now;
                link.last_beat_ms = 0;
                // Every connection re-reads the console. Opening the socket
                // sends no snapshot, and the document is the only source that
                // says which widget a state belongs to, so a desk that skipped
                // this would sit at unknown until someone touched the show.
                // It also catches a master that has loaded a different
                // workspace while the tablet was away.
                desk_set_link(&model, DESK_LINK_SYNCING);
                struct vc_doc fresh;
                if (fetch_console(host, port, &fresh) == 0) {
                    vc_free(&console);
                    console = fresh;
                    enabled = showmap_build(&model, &map, &console);
                    printf("desk: %d of %d controls enabled\n", enabled, map.count);
                }
                desk_set_link(&model, DESK_LINK_READY);
            }
        }

        struct pollfd fds[2];
        int count = 0;
        int touch_slot = -1, link_slot = -1;
        if (touch) {
            touch_slot = count;
            fds[count].fd = touch_fd;
            fds[count].events = POLLIN;
            count++;
        }
        if (link.ws) {
            link_slot = count;
            fds[count].fd = ws_fd(link.ws);
            fds[count].events = POLLIN;
            count++;
        }
        poll(fds, count, 100);
        now = now_ms();

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
                send_action(&link, action);
            }
        }

        if (link_slot >= 0 && link.ws) {
            char frame[4096];
            int r;
            while ((r = ws_recv_text(link.ws, frame, sizeof frame)) == 1) {
                link.last_heard_ms = now;
                apply_frame(&model, frame);
            }
            if (r < 0) {
                ws_close(link.ws);
                link.ws = NULL;
                desk_set_link(&model, DESK_LINK_DOWN);
            }
        }

        if (link.ws && now - link.last_beat_ms >= HEARTBEAT_MS) {
            link.last_beat_ms = now;
            if (ws_send_text(link.ws, "QLC+API|isProjectLoaded") != 0) {
                ws_close(link.ws);
                link.ws = NULL;
                desk_set_link(&model, DESK_LINK_DOWN);
            }
        }
        if (link.ws && now - link.last_heard_ms > STALE_MS) {
            fprintf(stderr, "desk: %lld ms without a word from the master\n",
                    (long long)(now - link.last_heard_ms));
            ws_close(link.ws);
            link.ws = NULL;
            desk_set_link(&model, DESK_LINK_DOWN);
        }

        if (model.dirty) {
            desk_paint(&canvas, &model, &fonts);
            model.dirty = 0;
            if (present_frame(present, &canvas) != 0)
                break;
            if (dump) {
                dump_ppm(&canvas, dump);
                break;
            }
        }
    }

    if (touch)
        touch_input_free(touch);
    if (touch_fd >= 0)
        close(touch_fd);
    if (link.ws)
        ws_close(link.ws);
    font_close(fonts.tile);
    font_close(fonts.value);
    font_close(fonts.label);
    free(canvas.px);
    present_close(present);
    vc_free(&console);
    return 0;
}
