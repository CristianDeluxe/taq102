// The lighting desk: the tablet as a control surface for the QLC+ show
// running on the Mac. It draws first, then dials: the session fetches the
// console the master has loaded, the show map is checked against it, the
// tiles are drawn, and one message goes out per gesture. What lights up is
// what the master says is running.
//
//   dmxdesk [--host 192.168.1.50] --map /etc/taq102/show-map.json
//
// The master comes from --host, else from /data/desk.conf, which the gear in
// the status bar writes: the settings surface joins a Wi-Fi, finds a running
// QLC+ on the subnet or takes a typed address, and sets the brightness.
// `dmxdesk --find 192.168.1.71/24 9999` is the sweep, run as a child.
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

#include "action_worker.h"
#include "canvas.h"
#include "desk_conf.h"
#include "desk_hold.h"
#include "desk_build_holds.h"
#include "desk_burst_base.h"
#include "desk_input.h"
#include "desk_lock.h"
#include "desk_layout.h"
#include "desk_layout_resolve.h"
#include "desk_model.h"
#include "desk_rebuild_model.h"
#include "desk_paint.h"
#include "desk_power.h"
#include "desk_present_drm.h"
#include "desk_setup.h"
#include "desk_setup_layout.h"
#include "desk_setup_paint.h"
#include "desk_speed.h"
#include "desk_speed_layout.h"
#include "desk_speed_paint.h"
#include "desk_view.h"
#include "display_power.h"
#include "font.h"
#include "iface_prefix.h"
#include "master_find.h"
#include "qlc_codec.h"
#include "perf_window.h"
#include "power_key.h"
#include "qlc_session.h"
#include "showmap.h"
#include "showmap_validate.h"
#include "status.h"
#include "touch_flip.h"
#include "touch_input.h"
#include "vcjson.h"
#include "wifi_conf.h"
#include "wifi_join.h"
#include "desk_wifi_request.h"
#include "wifi_scan.h"
#include "wifi_status.h"
#include "wpa_ctrl.h"

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
#define SCAN_TIMEOUT_MS 15000
#define FIND_TIMEOUT_S 40

#define DESK_CONF_PATH "/data/desk.conf"
#define WIFI_CONF_PATH "/data/wifi.conf"
#define SETTINGS_PATH "/data/taq102.conf"
#define WPA_SOCKET "/var/run/wpa_supplicant/wlan0"
#define FIND_OUT "/tmp/dmxdesk-find.txt"
#define FIND_TMP "/tmp/dmxdesk-find.tmp"

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

// The dial's echo, logged with its clock beside the frame that asked for it.
static void desk_apply_speed_echo(struct desk_speed *speed, int widget_id, int ms, int factor, int64_t now) {
    desk_speed_apply(speed, widget_id, ms, factor, now);
    fprintf(stderr, "speed: echo %d|SPEED_STATE|%d|%d at %lld\n", widget_id, ms, factor, (long long)now);
}

static void apply_frame(struct desk_model *model, struct desk_speed *speed, const char *frame,
                        int64_t now) {
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
    case QLC_SPEED_STATE:
        desk_apply_speed_echo(speed, msg.widget_id, msg.value, msg.factor, now);
        break;
    default:
        break;
    }
}

// A speed frame, logged with its clock so touch-to-echo can be read off the
// log: the tap's down edge is the frame's time, the echo is the push's.
static void send_speed(struct qlc_session *session, struct speed_action action, int64_t now) {
    char frame[64];
    int n = -1;
    switch (action.kind) {
    case SPEED_ACT_TIME:
    case SPEED_ACT_TIME_BOTH:
        n = qlc_encode_speed_ms(frame, sizeof frame, action.widget_id, action.ms);
        break;
    case SPEED_ACT_FACTOR:
        n = qlc_encode_speed_factor(frame, sizeof frame, action.widget_id, action.factor);
        break;
    case SPEED_ACT_NONE:
        return;
    }
    if (n > 0 && qlc_session_send(session, frame) == 0)
        fprintf(stderr, "speed: sent %s at %lld\n", frame, (long long)now);
    if (action.kind == SPEED_ACT_TIME_BOTH && action.widget_id2 >= 0) {
        n = qlc_encode_speed_ms(frame, sizeof frame, action.widget_id2, action.ms2);
        if (n > 0 && qlc_session_send(session, frame) == 0)
            fprintf(stderr, "speed: sent %s at %lld\n", frame, (long long)now);
    }
}

// A hold's frame: 255 on contact, 0 on release, cap or cancel; logged with
// its clock so a stuck output can be read off the log.
// Bursts sit in the hold model under a namespace of their own, so a
// function id never collides with a Flash button's widget id.
static void send_hold(struct desk_hold *hold, struct qlc_session *session, struct hold_action ha,
                      int64_t now) {
    if (ha.widget_id < 0)
        return;
    char frame[64];
    int n = ha.widget_id >= BURST_BASE
          ? qlc_encode_function_status(frame, sizeof frame, ha.widget_id - BURST_BASE, ha.on)
          : qlc_encode_flash(frame, sizeof frame, ha.widget_id, ha.on);
    if (n > 0 && qlc_session_send(session, frame) == 0) {
        fprintf(stderr, "hold: sent %s at %lld\n", frame, (long long)now);
        return;
    }
    fprintf(stderr, "hold: could not send %s at %lld\n", frame, (long long)now);
    // A release that did not go out is still owed: the output may be on.
    if (!ha.on)
        desk_hold_unsent(hold, ha.widget_id);
}

// Every hold released at once: a lock, a page change, the surface opening,
// a blank. Is are forgotten and the tiles repainted idle.
static void release_holds(struct desk_hold *hold, struct desk_model *model, struct qlc_session *session,
                          int *owner, int64_t now) {
    struct hold_action out[MAP_MAX_CONTROLS];
    int n = desk_hold_release_all(hold, now, out, MAP_MAX_CONTROLS);
    for (int k = 0; k < n; k++)
        send_hold(hold, session, out[k], now);
    for (int s = 0; s < TOUCH_MAX_SLOTS; s++) {
        if (owner[s] >= 0)
            desk_set_hold_progress(model, owner[s], -1, 0);
        owner[s] = -1;
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
    case DESK_ACT_STOP_ALL:
        n = qlc_encode_stop_all(frame, sizeof frame, action.widget_id);
        break;
    case DESK_ACT_NONE:
        return;
    }
    if (n > 0)
        qlc_session_send(session, frame);
}

// A retry every second and a half is one state to the operator, not a
// banner flipping between two words; the link is down only without a master.
static enum desk_link desk_link_of(enum qlc_link link, int have_master) {
    switch (link) {
    case QLC_READY:      return DESK_LINK_READY;
    case QLC_FETCHING:   return DESK_LINK_SYNCING;
    case QLC_CONNECTING: return DESK_LINK_CONNECTING;
    default:             return have_master ? DESK_LINK_CONNECTING : DESK_LINK_DOWN;
    }
}

// One `key=value` line out of a STATUS reply.
static void status_field(const char *reply, const char *key, char *out, size_t cap) {
    out[0] = '\0';
    size_t klen = strlen(key);
    for (const char *p = reply; p && *p; ) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            size_t n = len - klen - 1;
            if (n >= cap)
                n = cap - 1;
            memcpy(out, p + klen + 1, n);
            out[n] = '\0';
            return;
        }
        p = end ? end + 1 : NULL;
    }
}

// The subnet sweep in a child, its output in a file the loop reads when the
// worker exits. The shell gets the addresses as arguments, never in its text.
static int finder_start(struct action_worker *w, const char *exe, const char *addr, int port) {
    int prefix = iface_prefix("wlan0");
    if (prefix < 8 || prefix > 30)
        prefix = 24;
    char cidr[64], port_text[16];
    snprintf(cidr, sizeof cidr, "%.40s/%d", addr, prefix);
    snprintf(port_text, sizeof port_text, "%d", port);
    const char *argv[] = {
        "/bin/sh", "-c", "exec \"$0\" --find \"$1\" \"$2\" > " FIND_TMP " && mv " FIND_TMP " " FIND_OUT,
        exe, cidr, port_text, NULL,
    };
    unlink(FIND_OUT);
    return aw_start(w, argv, FIND_TIMEOUT_S);
}

static void finder_collect(struct desk_setup *setup) {
    char hosts[SETUP_FOUND_MAX][SETUP_HOST_MAX];
    int count = 0, partial = 0;
    FILE *f = fopen(FIND_OUT, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\n")] = '\0';
            if (strcmp(line, "partial") == 0)
                partial = 1;
            else if (desk_conf_valid_host(line) && count < SETUP_FOUND_MAX)
                snprintf(hosts[count++], SETUP_HOST_MAX, "%s", line);
        }
        fclose(f);
    }
    desk_setup_set_found(setup, hosts, count, partial);
    setup->master_busy[0] = '\0';
}

// The known networks, matched against a scan for the card's "known" tag.
static void mark_known(struct desk_setup *setup, const struct wifi_scan *scan) {
    struct wifi_conf conf;
    int known[WIFI_SCAN_MAX] = { 0 };
    if (wifi_conf_read(WIFI_CONF_PATH, &conf) >= 0)
        for (int i = 0; i < scan->count; i++)
            known[i] = wifi_conf_knows(&conf, scan->network[i].ssid);
    desk_setup_set_scan(setup, scan, known);
}

static const char *link_word_of(enum qlc_link link) {
    switch (link) {
    case QLC_READY:      return "Linked";
    case QLC_FETCHING:   return "Reading the show";
    case QLC_CONNECTING: return "Connecting";
    default:             return "Not linked";
    }
}

static int in_gear(int x, int y) {
    return desk_rect_contains(desk_view_gear(), x, y);
}

// The bar's status is read once a second; only a change repaints.
static int status_changed(const struct status *a, const struct status *b) {
    return a->have_batt != b->have_batt || a->cap != b->cap || a->plugged != b->plugged ||
           status_wifi_bars(a) != status_wifi_bars(b);
}

int main(int argc, char **argv) {
    const char *host = NULL;
    int port = 9999;
    const char *map_path = "/etc/taq102/vibra.desk.json";
    const char *card = "/dev/dri/card0";
    const char *touch_device = "/dev/input/event1";
    const char *power_device = NULL;
    int view_page = 0, view_bank = 0;    // --view P,B: the first page shown, for dumps
    int start_setup = 0;                 // --setup: the surface open at start, for dumps

    if (argc == 4 && !strcmp(argv[1], "--find"))
        return master_find_run(argv[2], atoi(argv[3]), stdout) < 0 ? 2 : 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) map_path = argv[++i];
        else if (!strcmp(argv[i], "--card") && i + 1 < argc) card = argv[++i];
        else if (!strcmp(argv[i], "--touch") && i + 1 < argc) touch_device = argv[++i];
        else if (!strcmp(argv[i], "--power") && i + 1 < argc) power_device = argv[++i];
        else if (!strcmp(argv[i], "--setup")) start_setup = 1;
        else if (!strcmp(argv[i], "--view") && i + 1 < argc) {
            if (sscanf(argv[++i], "%d,%d", &view_page, &view_bank) != 2)
                view_page = view_bank = 0;
        }
        else {
            fprintf(stderr, "usage: %s [--host H] [--port P] [--map FILE]"
                            " [--card /dev/dri/cardN] [--touch /dev/input/eventN]"
                            " [--power /dev/input/eventN] [--view PAGE,BANK] [--setup]\n"
                            "       %s --find ADDR/PREFIX PORT\n",
                    argv[0], argv[0]);
            return 2;
        }
    }
    // --host is for the bench; the tablet keeps its master in its own file.
    struct desk_conf conf;
    desk_conf_defaults(&conf);
    if (desk_conf_load(&conf, DESK_CONF_PATH) != 0)
        fprintf(stderr, "desk: cannot read %s\n", DESK_CONF_PATH);
    if (host) {
        snprintf(conf.master, sizeof conf.master, "%s", host);
        conf.port = port;
    }
    if (!desk_conf_valid_port(conf.port)) {
        fprintf(stderr, "dmxdesk: bad port\n");
        return 2;
    }
    if (!conf.master[0])
        fprintf(stderr, "desk: no master set; the gear in the status bar sets one\n");

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
    struct canvas canvas = { .px = calloc((size_t)w * h, 4), .w = w, .h = h };
    if (!canvas.px) {
        present_close(present);
        return 1;
    }
    struct desk_fonts fonts;
    if (desk_fonts_open(&fonts, "/usr/share/fonts/taq102") != 0)
        fprintf(stderr, "desk: fonts missing under /usr/share/fonts/taq102; 3x5 glyphs it is\n");

    struct vc_doc console;
    memset(&console, 0, sizeof console);
    struct desk_model model;
    int enabled = showmap_build(&model, &map, &console);
    printf("desk: %s, %d of %d controls enabled before the first snapshot\n",
           map.key, enabled, map.count);
    // The map's controls come first in the model, then the master and the
    // panic button, which is what the resolver is told.
    struct desk_layout layout;
    if (desk_layout_resolve(&map, map.count, map.count + 1, map.count + 2, map.count + 3, &layout) != 0) {
        fprintf(stderr, "desk: the map does not fit the screen\n");
        return 1;
    }
    desk_set_layout(&model, &layout);
    desk_set_view(&model, view_page, view_bank);
    struct desk_input input;
    desk_input_init(&input);
    struct desk_speed speed;
    desk_speed_init(&speed, &map);
    int speed_slot = -1;        // the finger the speed cards own, if any
    int last_page = model.page;
    struct desk_hold hold;
    int hold_owner[TOUCH_MAX_SLOTS];    // the control each finger holds, or -1
    desk_build_holds(&hold, &model, hold_owner);

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

    // The power key blanks and locks; without a node the desk simply never
    // blanks, which is show mode anyway.
    int power_fd = power_device ? open(power_device, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    if (power_device && power_fd < 0)
        fprintf(stderr, "no power key on %s: the desk will not blank\n", power_device);
    struct power_key power_key = { { 0 }, 0 };
    struct desk_lock lock;
    desk_lock_init(&lock);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGUSR1, on_snapshot);

    struct qlc_session_config cfg = {
        .port = port, .heartbeat_ms = HEARTBEAT_MS, .stale_ms = STALE_MS,
        .reconnect_ms = RECONNECT_MS, .connect_timeout_ms = CONNECT_TIMEOUT_MS,
        .fetch_timeout_ms = FETCH_TIMEOUT_MS, .snapshot_limit = VC_LIMIT,
    };
    snprintf(cfg.host, sizeof cfg.host, "%s", conf.master);
    cfg.port = conf.port;
    struct qlc_session *session = qlc_session_new(&cfg);
    if (!session)
        return 1;

    // The settings surface and what it drives: the backlight, the
    // supplicant, the join, the finder. Each is optional; the card says so.
    int64_t start_ms = now_ms();
    struct desk_power power;
    if (desk_power_init(&power, SETTINGS_PATH, "/sys/class/backlight", start_ms) != 0)
        return 1;
    struct desk_setup setup;
    desk_setup_init(&setup);
    setup.brightness_max = power.max;
    setup.brightness = power.level;
    setup.power_aware = desk_power_aware(&power);
    desk_setup_set_master(&setup, conf.master, conf.port, conf.master[0] != '\0');
    struct wpa_ctrl *wpa = wpa_ctrl_open(WPA_SOCKET);
    setup.wifi_available = wpa != NULL;
    if (!wpa)
        fprintf(stderr, "desk: no supplicant control at %s: the Wi-Fi card is read-only\n", WPA_SOCKET);
    struct wifi_join join;
    wifi_join_init(&join, wpa, WIFI_CONF_PATH);
    struct action_worker *finder = aw_new();
    if (!finder)
        return 1;
    char exe[256];
    ssize_t exe_len = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (exe_len > 0)
        exe[exe_len] = '\0';
    else
        snprintf(exe, sizeof exe, "%s", argv[0]);
    struct desk_wifi_request wifi_request = {0};
    int64_t scan_started_ms = 0;
    int scan_dispatched = 0;    // the queued SCAN has actually gone to the daemon
    int setup_slot = -1;        // the one finger the settings surface owns
    int gear_slot = -1;
    if (start_setup) {
        desk_setup_open(&setup);
        if (wpa) {
            wifi_request.scan_queued = 1;
            snprintf(setup.wifi_busy, sizeof setup.wifi_busy, "Scanning");
            scan_started_ms = start_ms;
        }
    }

    const char *dump = getenv("DMXDESK_DUMP");
    // DMXDESK_RTT=1 prints every heartbeat's round trip: the raw material for
    // deciding whether 750 ms of silence is a dead master or a slow Wi-Fi.
    int log_rtt = getenv("DMXDESK_RTT") != NULL;
    int last_rtt_logged = -1;
    // DMXDESK_PERF=1 keeps the last thousand paint and present times and
    // prints their p50/p95 with RSS and MemAvailable every ten seconds: the
    // numbers the perf gate is argued from.
    int log_perf = getenv("DMXDESK_PERF") != NULL;
    // DMXDESK_DRAG=1 moves the master's requested level every loop and
    // repaints, sending nothing: the load of a finger dragging a fader all
    // night, for the perf gate, on a kernel with no uinput to fake a finger.
    int fake_drag = getenv("DMXDESK_DRAG") != NULL;
    struct perf_window paint_ms, present_ms;
    perf_window_init(&paint_ms);
    perf_window_init(&present_ms);
    int force_flip = getenv("DMXDESK_FLIP") != NULL;
    unsigned long flips = 0;
    int64_t last_report_ms = now_ms();
    int64_t last_status_ms = 0;
    struct status status;
    memset(&status, 0, sizeof status);
    int bar_ready = 0;      // the bar's facts reach the model on the next status tick
    enum qlc_link last_link = QLC_DOWN;
    char last_reason[96] = "";

    while (!stop) {
        struct pollfd fds[6];
        int count = 0, touch_slot = -1, power_slot = -1;
        if (touch) {
            touch_slot = count;
            fds[count].fd = touch_fd;
            fds[count].events = POLLIN;
            fds[count].revents = 0;
            count++;
        }
        if (power_fd >= 0) {
            power_slot = count;
            fds[count].fd = power_fd;
            fds[count].events = POLLIN;
            fds[count].revents = 0;
            count++;
        }
        if (wpa) {
            fds[count].fd = wpa_ctrl_event_fd(wpa);
            fds[count].events = POLLIN;
            fds[count].revents = 0;
            count++;
            fds[count].fd = wpa_ctrl_request_fd(wpa);
            fds[count].events = POLLIN;
            fds[count].revents = 0;
            count++;
        }
        count += qlc_session_pollfds(session, fds + count, 2);
        // Forced flips run at the panel's own pace, not the loop's.
        poll(fds, (nfds_t)count, force_flip || fake_drag ? 0 : 100);
        int64_t now = now_ms();
        if (fake_drag) {
            for (int i = 0; i < model.count; i++)
                if (model.control[i].kind == DESK_MASTER) {
                    model.control[i].pressed = 1;
                    model.control[i].requested_level = (int)((now / 8) % 256);
                    const struct desk_placement *mp = desk_placement_of(&model, i);
                    if (mp)
                        desk_damage_rect(&model, mp->x, mp->y, mp->w, mp->h);
                }
        }

        if (power_slot >= 0 && (fds[power_slot].revents & POLLIN) &&
            power_key_read(&power_key, power_fd)) {
            desk_lock_power_key(&lock, now);
            desk_cancel_all(&model);
            // Every finger's claim goes with the display: the desk's own
            // slot bookkeeping too, or a reused slot would inherit a gesture.
            desk_speed_touch_cancel(&speed);
            desk_setup_touch_cancel(&setup);
            desk_input_cancel_all(&input);
            release_holds(&hold, &model, session, hold_owner, now);
            gear_slot = setup_slot = speed_slot = -1;
            if (setup.open) {
                desk_setup_close(&setup);
                bar_ready = 0;
                last_status_ms = 0;
            }
            if (touch) {
                // The fingers the display took with it are lifted for the
                // lock's count too, or nothing would count after the wake.
                struct touch_event dropped[32];
                int gone = touch_input_cancel_all(touch, dropped, 32);
                for (int d = 0; d < gone; d++)
                    desk_lock_contact(&lock, 0);
            }
            if (lock.state == DESK_BLANKED) {
                if (display_power_off() != 0)
                    fprintf(stderr, "desk: cannot blank the display\n");
            } else {
                if (display_power_on() != 0)
                    fprintf(stderr, "desk: cannot wake the display\n");
                model.dirty = 1;
            }
            fprintf(stderr, "desk: power key, surface %s\n",
                    lock.state == DESK_BLANKED ? "blanked" : "locked");
        }

        if (touch_slot >= 0 && (fds[touch_slot].revents & POLLIN)) {
            struct touch_event events[32];
            int n = touch_input_read_fd(touch, touch_fd, events, 32);
            for (int i = 0; i < n; i++) {
                if (events[i].kind == TOUCH_DOWN)
                    desk_lock_contact(&lock, 1);
                else if (events[i].kind == TOUCH_UP || events[i].kind == TOUCH_CANCEL)
                    desk_lock_contact(&lock, 0);
                int index;
                int slot = events[i].slot;
                int x = (int)events[i].x, y = (int)events[i].y;
                // The gear: a tap opens or closes the settings surface, when
                // the surface is allowed at all.
                if (slot >= 0 && slot < TOUCH_MAX_SLOTS) {
                    if (events[i].kind == TOUCH_DOWN && in_gear(x, y) && desk_lock_allows(&lock)) {
                        gear_slot = slot;
                        continue;
                    }
                    if (slot == gear_slot) {
                        if (events[i].kind == TOUCH_UP || events[i].kind == TOUCH_CANCEL) {
                            gear_slot = -1;
                            if (events[i].kind == TOUCH_UP && in_gear(x, y)) {
                                // A modal transition: every gesture under it ends.
                                desk_cancel_all(&model);
                                desk_input_cancel_all(&input);
                                release_holds(&hold, &model, session, hold_owner, now);
                                desk_speed_touch_cancel(&speed);
                                desk_speed_reset_taps(&speed);
                                speed_slot = -1;
                                if (setup.open) {
                                    desk_setup_close(&setup);
                                } else {
                                    desk_setup_open(&setup);
                                    if (wpa && !setup.wifi_busy[0] && !join.running) {
                                        wifi_request.scan_queued = 1;
                                        snprintf(setup.wifi_busy, sizeof setup.wifi_busy, "Scanning");
                                        scan_started_ms = now;
                                    }
                                }
                                bar_ready = 0;
                                last_status_ms = 0;
                                desk_set_status(&model, model.battery, model.charging, model.wifi_bars, setup.open);
                            }
                        }
                        continue;
                    }
                    // The surface owns one contact, the first that lands on
                    // it; a second finger on the sheet is swallowed so it can
                    // neither steal nor end the first one's gesture. The
                    // master column beside the sheet stays the desk's.
                    if (setup.open && events[i].kind == TOUCH_DOWN && x < SETUP_SHEET_W) {
                        if (setup_slot >= 0)
                            continue;
                        setup_slot = slot;
                    }
                    if (slot == setup_slot) {
                        struct setup_action act;
                        memset(&act, 0, sizeof act);
                        switch (events[i].kind) {
                        case TOUCH_DOWN: act = desk_setup_touch_down(&setup, x, y); break;
                        case TOUCH_MOVE: act = desk_setup_touch_move(&setup, x, y); break;
                        case TOUCH_UP: act = desk_setup_touch_up(&setup, x, y); setup_slot = -1; break;
                        case TOUCH_CANCEL: desk_setup_touch_cancel(&setup); setup_slot = -1; break;
                        }
                        switch (act.kind) {
                        case SETUP_NONE:
                            break;
                        case SETUP_CLOSE:
                            desk_setup_close(&setup);
                            bar_ready = 0;
                            last_status_ms = 0;
                            break;
                        case SETUP_SCAN: {
                            if (wpa) {
                                wifi_request.scan_queued = 1;
                                snprintf(setup.wifi_busy, sizeof setup.wifi_busy, "Scanning");
                                scan_started_ms = now;
                            } else {
                                snprintf(setup.wifi_note, sizeof setup.wifi_note, "Scan refused");
                            }
                            break;
                        }
                        case SETUP_JOIN:
                            if (!wpa) {
                                setup.wifi_busy[0] = '\0';
                                break;
                            }
                            if (wifi_join_start(&join, act.ssid, act.psk[0] ? act.psk : NULL,
                                                act.known, setup.ssid, now) != 0) {
                                setup.wifi_busy[0] = '\0';
                                snprintf(setup.wifi_note, sizeof setup.wifi_note, "%.*s",
                                         (int)sizeof setup.wifi_note - 1, join.reason);
                            } else {
                                snprintf(setup.wifi_busy, sizeof setup.wifi_busy, "%s", join.word);
                            }
                            fprintf(stderr, "desk: join %s: %s\n", act.ssid,
                                    join.running ? "started" : join.reason);
                            break;
                        case SETUP_FIND:
                            setup.master_note[0] = '\0';
                            if (!status.have_wifi || !status.addr[0]) {
                                snprintf(setup.master_note, sizeof setup.master_note, "No network address");
                                fprintf(stderr, "desk: find: no address on wlan0\n");
                            } else if (finder_start(finder, exe, status.addr, setup.port) == 0) {
                                snprintf(setup.master_busy, sizeof setup.master_busy, "Finding");
                            } else {
                                snprintf(setup.master_note, sizeof setup.master_note, "Search failed");
                                fprintf(stderr, "desk: find: cannot start the sweep\n");
                            }
                            break;
                        case SETUP_FIND_CANCEL:
                            // The sweep dies now; its poll reports it stopped
                            // and the card says so.
                            aw_cancel(finder);
                            snprintf(setup.master_note, sizeof setup.master_note, "Search stopped");
                            break;
                        case SETUP_SET_MASTER:
                            snprintf(conf.master, sizeof conf.master, "%s", act.host);
                            conf.port = act.port;
                            if (desk_conf_save(&conf, DESK_CONF_PATH) != 0) {
                                snprintf(setup.save_note, sizeof setup.save_note, "Applied, not saved");
                                fprintf(stderr, "desk: cannot save %s\n", DESK_CONF_PATH);
                            } else {
                                setup.save_note[0] = '\0';
                            }
                            qlc_session_set_host(session, conf.master, conf.port);
                            desk_setup_set_master(&setup, conf.master, conf.port, 1);
                            fprintf(stderr, "desk: master %s:%d\n", conf.master, conf.port);
                            break;
                        case SETUP_BRIGHTNESS:
                            desk_power_set_level(&power, act.value, now);
                            break;
                        case SETUP_POWER_AWARE:
                            desk_power_set_aware(&power, act.value, now);
                            break;
                        }
                        memset(&act, 0, sizeof act);
                        continue;
                    }
                }
                // On the SPEED page the content under the state row is the
                // cards': a contact the model does not take goes to them.
                if (layout.speed_page >= 0 && model.page == layout.speed_page && slot == speed_slot) {
                    if (events[i].kind == TOUCH_UP) {
                        // A release after the lock fires nothing.
                        struct speed_action sa = desk_speed_touch_up(&speed, x, y, now_ms());
                        if (desk_lock_allows(&lock))
                            send_speed(session, sa, now);
                        speed_slot = -1;
                    } else if (events[i].kind == TOUCH_CANCEL) {
                        desk_speed_touch_cancel(&speed);
                        speed_slot = -1;
                    }
                    continue;
                }
                // A hold or the tempo card: pressed on contact, owned per finger,
                // never a model capture. A hold fires only unlocked and linked.
                if (slot >= 0 && slot < TOUCH_MAX_SLOTS && hold_owner[slot] >= 0) {
                    int ci = hold_owner[slot];
                    if (events[i].kind == TOUCH_UP || events[i].kind == TOUCH_CANCEL) {
                        struct desk_control *hc = &model.control[ci];
                        if ((hc->kind == DESK_HOLD || hc->kind == DESK_BURST) && hc->hold_index >= 0)
                            send_hold(&hold, session, desk_hold_release(&hold, hc->hold_index, slot, now), now);
                        desk_set_hold_progress(&model, ci, -1, 0);
                        hold_owner[slot] = -1;
                    }
                    continue;
                }
                if (events[i].kind == TOUCH_DOWN && slot >= 0 && slot < TOUCH_MAX_SLOTS &&
                    desk_lock_allows(&lock)) {
                    int ci = desk_control_at(&model, x, y);
                    if (ci >= 0 && (model.control[ci].kind == DESK_HOLD || model.control[ci].kind == DESK_BURST)) {
                        struct desk_control *hc = &model.control[ci];
                        if (hc->enabled && hc->hold_index >= 0 && model.link == DESK_LINK_READY) {
                            struct hold_action ha = desk_hold_press(&hold, hc->hold_index, slot, now);
                            if (ha.widget_id >= 0) {
                                send_hold(&hold, session, ha, now);
                                hold_owner[slot] = ci;
                                desk_set_hold_progress(&model, ci, 0, 1);
                            }
                        }
                        continue;
                    }
                    if (ci >= 0 && model.control[ci].kind == DESK_TEMPO && model.control[ci].enabled) {
                        const struct desk_placement *tp = desk_placement_of(&model, ci);
                        if (!tp)
                            continue;
                        int tx, ty, tw, th;
                        desk_speed_tempo_tap_rect(tp->x, tp->y, tp->w, tp->h, &tx, &ty, &tw, &th);
                        if (x >= tx && x < tx + tw && y >= ty && y < ty + th) {
                            int64_t at = events[i].t > 0 ? (int64_t)(events[i].t * 1000.0) : now;
                            send_speed(session, desk_speed_tap(&speed, 0, now_ms(), at), now);
                            hold_owner[slot] = ci;      // the release is consumed, the target reads pressed
                            desk_set_hold_progress(&model, ci, -1, 1);
                        }
                        continue;
                    }
                }
                enum desk_target target = desk_input_feed(&input, &model, &events[i], &index);
                if (target == TARGET_LOCK) {
                    int changed = events[i].kind == TOUCH_DOWN ? desk_lock_target_down(&lock, now)
                                : events[i].kind == TOUCH_UP ? desk_lock_target_up(&lock, now) : 0;
                    if (changed) {
                        desk_cancel_all(&model);
                        desk_input_cancel_all(&input);
                        release_holds(&hold, &model, session, hold_owner, now);
                        desk_speed_touch_cancel(&speed);
                        desk_speed_reset_taps(&speed);
                        speed_slot = -1;
                        fprintf(stderr, "desk: surface %s\n",
                                lock.state == DESK_LOCKED ? "locked" : "unlocked");
                    }
                    continue;
                }
                if (!desk_lock_allows(&lock))
                    continue;
                if (target == TARGET_RAIL) {
                    if (index >= 0)
                        desk_set_view(&model, index, -1);
                    continue;
                }
                if (target == TARGET_BANK) {
                    if (index >= 0)
                        desk_set_view(&model, model.page, index);
                    continue;
                }
                if (target != TARGET_CONTENT)
                    continue;
                struct desk_action action = { DESK_ACT_NONE, -1, 0 };
                switch (events[i].kind) {
                case TOUCH_DOWN:
                    action = desk_touch_down(&model, events[i].slot,
                                             (int)events[i].x, (int)events[i].y);
                    if (layout.speed_page >= 0 && model.page == layout.speed_page &&
                        model.capture_slot != slot && speed_slot < 0) {
                        // Tempo is measured from the contact's own clock, not
                        // from when the batch was drained.
                        int64_t at = events[i].t > 0 ? (int64_t)(events[i].t * 1000.0) : now;
                        struct speed_action sa = desk_speed_touch_down(&speed, x, y, now_ms(), at);
                        // Dead space claims nothing: another finger may still
                        // reach a target while this one rests on the card.
                        if (speed.capture != SPEED_T_NONE)
                            speed_slot = slot;
                        send_speed(session, sa, now);
                        continue;
                    }
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

        // The supplicant's events: a scan's end fills the card; every line
        // reaches a running join, which also watches the address.
        if (wpa) {
            char ev[512];
            while (wpa_ctrl_event(wpa, ev, sizeof ev) == 1) {
                if (strstr(ev, "CTRL-EVENT-SCAN-RESULTS"))
                    wifi_request.results_queued = 1;
                if (join.running && wifi_request.pending == DESK_WIFI_NONE)
                    wifi_join_step(&join, now, ev, status.addr);
            }
            static char reply[16384];
            enum desk_wifi_command done = desk_wifi_request_step(&wifi_request, wpa,
                now, join.running, setup.open, reply, sizeof reply);
            // FAIL-BUSY means a scan is already under way: its results will
            // come on the same event, so it is not a refusal.
            if (done == DESK_WIFI_SCAN && (wifi_request.result < 0 ||
                (strncmp(reply, "OK", 2) != 0 && strncmp(reply, "FAIL-BUSY", 9) != 0))) {
                if (!join.running)
                    setup.wifi_busy[0] = '\0';
                snprintf(setup.wifi_note, sizeof setup.wifi_note, "Scan refused");
                fprintf(stderr, "wifi: SCAN answered %d: %.40s\n", wifi_request.result, reply);
                setup.dirty = 1;
            } else if (done == DESK_WIFI_RESULTS) {
                if (wifi_request.result > 0 && strncmp(reply, "FAIL", 4) != 0) {
                    struct wifi_scan scan;
                    wifi_scan_parse(reply, strlen(reply), &scan);
                    mark_known(&setup, &scan);
                } else {
                    snprintf(setup.wifi_note, sizeof setup.wifi_note, "Scan results unavailable");
                }
                if (strcmp(setup.wifi_busy, "Scanning") == 0)
                    setup.wifi_busy[0] = '\0';
                setup.dirty = 1;
            } else if (done == DESK_WIFI_STATUS && wifi_request.result > 0 && setup.open) {
                char ssid[WIFI_SSID_MAX], state[SETUP_WORD_MAX];
                status_field(reply, "ssid", ssid, sizeof ssid);
                status_field(reply, "wpa_state", state, sizeof state);
                if (strcmp(ssid, setup.ssid) != 0 || strcmp(state, setup.wifi_state) != 0 ||
                    strcmp(status.addr, setup.address) != 0)
                    desk_setup_set_wifi(&setup, ssid, state, status.addr);
            }
            if (join.running && wifi_request.pending == DESK_WIFI_NONE)
                wifi_join_step(&join, now, NULL, status.addr);
            if (join.running) {
                if (strcmp(setup.wifi_busy, join.word) != 0) {
                    snprintf(setup.wifi_busy, sizeof setup.wifi_busy, "%s", join.word);
                    setup.dirty = 1;
                }
            } else if (join.state == WIFI_JOIN_DONE || join.state == WIFI_JOIN_FAILED) {
                if (join.state == WIFI_JOIN_DONE)
                    snprintf(setup.wifi_note, sizeof setup.wifi_note, "Joined %s", join.ssid);
                else
                    snprintf(setup.wifi_note, sizeof setup.wifi_note, "%.*s",
                                         (int)sizeof setup.wifi_note - 1, join.reason);
                fprintf(stderr, "desk: join %s: %s\n", join.ssid, setup.wifi_note);
                join.state = WIFI_JOIN_IDLE;
                setup.wifi_busy[0] = '\0';
                setup.dirty = 1;
                mark_known(&setup, &setup.scan);
            }
            // A queued scan waits behind a join: its busy word and its clock
            // start when the command actually goes out, not when it was asked.
            if (wifi_request.pending == DESK_WIFI_SCAN && !scan_dispatched) {
                scan_dispatched = 1;
                scan_started_ms = now;
                if (!join.running) {
                    snprintf(setup.wifi_busy, sizeof setup.wifi_busy, "Scanning");
                    setup.dirty = 1;
                }
            } else if (wifi_request.pending != DESK_WIFI_SCAN && !wifi_request.scan_queued) {
                scan_dispatched = 0;
            }
            if (setup.wifi_busy[0] && strcmp(setup.wifi_busy, "Scanning") == 0 &&
                scan_dispatched && now - scan_started_ms > SCAN_TIMEOUT_MS) {
                setup.wifi_busy[0] = '\0';
                snprintf(setup.wifi_note, sizeof setup.wifi_note, "Scan timed out");
                setup.dirty = 1;
            }
        }
        if (setup.master_busy[0]) {
            int exit_status = 0;
            if (aw_poll(finder, &exit_status) || !aw_busy(finder)) {
                finder_collect(&setup);
                if (setup.found_count == 0 && strcmp(setup.master_note, "Search stopped") != 0)
                    snprintf(setup.master_note, sizeof setup.master_note, "%s",
                             exit_status != 0 ? "Search failed" : "No QLC+ on this network");
                fprintf(stderr, "desk: find: %d master%s%s\n", setup.found_count,
                        setup.found_count == 1 ? "" : "s", setup.found_partial ? " (partial)" : "");
            }
        }

        enum qlc_link link = qlc_session_step(session, now);
        struct vc_doc fresh;
        if (qlc_session_take_snapshot(session, &fresh)) {
            vc_free(&console);
            console = fresh;
            // A finger on a hit while the model is rebuilt: its release goes
            // out now, from the model that knows it, or the output stays on.
            release_holds(&hold, &model, session, hold_owner, now);
            enabled = desk_rebuild_model(&model, &map, &console, &layout);
            desk_build_holds(&hold, &model, hold_owner);
            if (showmap_mismatch(&map, &console))
                desk_speed_disable(&speed, "show mismatch");
            else
                desk_speed_validate(&speed, &console);
            printf("desk: %d of %d controls enabled against the master's console\n",
                   enabled, map.count);
        }
        char frame[4096];
        while (qlc_session_recv(session, frame, sizeof frame) == 1)
            apply_frame(&model, &speed, frame, now);
        desk_set_link(&model, desk_link_of(link, conf.master[0] != '\0'));
        desk_speed_set_link(&speed, link == QLC_READY, now);
        desk_speed_tick(&speed, now);
        if (speed.refresh_wanted) {
            speed.refresh_wanted = 0;
            qlc_session_refresh(session, now);
            fprintf(stderr, "speed: no echo, re-reading the show\n");
        }
        if (model.page != last_page) {
            last_page = model.page;
            desk_speed_reset_taps(&speed);
            desk_speed_touch_cancel(&speed);
            speed_slot = -1;
            release_holds(&hold, &model, session, hold_owner, now);
        }
        // Holds: the cap, the link, the owed releases, the tiles' countdown.
        {
            struct hold_action out[MAP_MAX_CONTROLS];
            int n = desk_hold_tick(&hold, now, out, MAP_MAX_CONTROLS);
            for (int k = 0; k < n; k++)
                send_hold(&hold, session, out[k], now);
            desk_hold_set_link(&hold, link == QLC_READY);
            if (link == QLC_READY) {
                n = desk_hold_owed(&hold, out, MAP_MAX_CONTROLS);
                for (int k = 0; k < n; k++)
                    send_hold(&hold, session, out[k], now);
            }
            // A hold whose release is still owed shows it: the output may be on.
            for (int i = 0; i < model.count; i++) {
                struct desk_control *c = &model.control[i];
                if ((c->kind != DESK_HOLD && c->kind != DESK_BURST) || c->hold_index < 0 || c->pressed)
                    continue;
                int unresolved = desk_hold_unresolved(&hold, c->hold_index);
                desk_set_hold_progress(&model, i, unresolved ? -2 : -1, 0);
            }
            for (int s = 0; s < TOUCH_MAX_SLOTS; s++) {
                if (hold_owner[s] < 0 || (model.control[hold_owner[s]].kind != DESK_HOLD &&
                                          model.control[hold_owner[s]].kind != DESK_BURST))
                    continue;
                int hi = model.control[hold_owner[s]].hold_index;
                int progress = hi >= 0 ? desk_hold_progress(&hold, hi, now) : -1;
                if (progress >= 0)
                    progress = (progress / 50) * 50;    // twenty steps, not a repaint per millisecond
                desk_set_hold_progress(&model, hold_owner[s], progress, 1);
            }
        }
        if (speed.dirty && layout.speed_page >= 0 && model.page == layout.speed_page) {
            desk_damage_rect(&model, SPEED_CARD_X, SPEED_CARD_Y(0), SPEED_CARD_W,
                             SPEED_BOTH_Y + SPEED_BOTH_H - SPEED_CARD_Y(0));
        }
        speed.dirty = 0;
        desk_set_locked(&model, lock.state != DESK_UNLOCKED);
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

        if (setup.open && strcmp(setup.link_word, link_word_of(link)) != 0) {
            snprintf(setup.link_word, sizeof setup.link_word, "%s", link_word_of(link));
            setup.dirty = 1;
        }

        if (now - last_status_ms >= STATUS_MS) {
            last_status_ms = now;
            struct status next;
            status_read(&next);
            desk_power_tick(&power, &next, now);
            if (setup.open) {
                if (power.level != setup.brightness && !setup.dragging_fader) {
                    setup.brightness = power.level;
                    setup.dirty = 1;
                }
                if (setup.brightness_unsaved != power.save_failed) {
                    setup.brightness_unsaved = power.save_failed;
                    setup.dirty = 1;
                }
                if (!wpa && strcmp(next.addr, setup.address) != 0) {
                    char ssid[WIFI_SSID_MAX] = "";
                    wifi_read_ssid(WIFI_CONF_PATH, ssid, sizeof ssid);
                    desk_setup_set_wifi(&setup, ssid, next.have_wifi ? "COMPLETED" : "", next.addr);
                }
            }
            if (status_changed(&status, &next) || !bar_ready) {
                status = next;
                bar_ready = 1;
            }
            status = next;
            desk_set_status(&model, status.have_batt ? status.cap : -1, status.have_batt && status.ma > 0,
                            status.have_wifi ? status_wifi_bars(&status) : 0, setup.open);
        }

        // A blanked display is not painted: the CRTC is off and a flip would
        // only wake the pipeline the operator just switched off.
        if (lock.state == DESK_BLANKED)
            model.dirty = 0;
        if (setup.dirty) {
            setup.dirty = 0;
            desk_damage_rect(&model, SETUP_SHEET_X, SETUP_SHEET_Y, SETUP_SHEET_W, SETUP_SHEET_H);
        }
        int dx = 0, dy = 0, dw = -1, dh = -1;
        int damaged = desk_take_damage(&model, &dx, &dy, &dw, &dh);
        if (damaged || force_flip) {
            // A forced flip re-presents the same canvas: the point is the
            // flip, not the paint, and a full repaint on this CPU would cap
            // the rate far below the panel's. A damaged frame paints only
            // its rectangle: every primitive clips to it.
            if (damaged) {
                int64_t t0 = now_ms();
                if (dw > 0)
                    canvas_set_clip(&canvas, dx, dy, dw, dh);
                desk_paint(&canvas, &model, &fonts);
                {
                    // SHOW's tempo card: the numbers and the TAP target are the speed model's.
                    int ti = DESK_TEMPO_INDEX(&map);
                    const struct desk_placement *tp = ti < model.count ? desk_placement_of(&model, ti) : NULL;
                    if (tp && model.control[ti].enabled)
                        desk_speed_paint_tempo(&canvas, &speed, 0, &fonts, tp->x, tp->y, tp->w, tp->h,
                                               model.control[ti].pressed);
                }
                if (layout.speed_page >= 0 && model.page == layout.speed_page) {
                    desk_speed_paint(&canvas, &speed, &fonts);
                    desk_paint_overlays(&canvas, &model, &fonts);
                }
                desk_setup_paint(&canvas, &setup, &fonts);
                canvas_clear_clip(&canvas);
                if (log_perf)
                    perf_window_add(&paint_ms, (int)(now_ms() - t0));
            }
            int64_t t1 = now_ms();
            if (present_frame_damage(present, &canvas, dx, dy, dw, dh) != 0)
                break;
            if (log_perf)
                perf_window_add(&present_ms, (int)(now_ms() - t1));
            flips++;
            // A dump waits for the link, so the frame shows the show and not
            // the banner; six seconds without one and it shows that instead.
            if (dump && (link == QLC_READY || now - start_ms > 6000)) {
                dump_ppm(&canvas, dump);
                break;
            }
        }
        if (now - last_report_ms >= 10000) {
            last_report_ms = now;
            printf("desk: %lu flips so far, link %s (%s)\n", flips,
                   link == QLC_READY ? "up" : "down", qlc_session_reason(session));
            if (log_perf) {
                printf("perf: paint p50 %d p95 %d ms (%d) present p50 %d p95 %d ms (%d)"
                       " rss %ld kB memavail %ld kB\n",
                       perf_window_percentile(&paint_ms, 50), perf_window_percentile(&paint_ms, 95),
                       paint_ms.count, perf_window_percentile(&present_ms, 50),
                       perf_window_percentile(&present_ms, 95), present_ms.count,
                       perf_rss_kb(), perf_memavailable_kb());
            }
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
    if (power_fd >= 0)
        close(power_fd);
    qlc_session_free(session);
    wifi_join_free(&join);
    if (wpa)
        wpa_ctrl_close(wpa);
    aw_free(finder);
    desk_power_free(&power);
    desk_fonts_close(&fonts);
    free(canvas.px);
    present_close(present);
    vc_free(&console);
    return 0;
}
