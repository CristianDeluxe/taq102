/* Scripted protocol-B input using the Linux 4.4 uinput ABI.
 * Screen coordinates default to the advertised 2048x1536 range. Set
 * TOUCHSIM_NATIVE=1 for glcube: the stock firmware emits panel pixels even
 * though gsl3673 advertises that larger range. No orientation is implied.
 */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int touch_fd = -1, power_fd = -1, native_coordinates;
static volatile sig_atomic_t stopped;
static int tracking_id;

static void stop(int signal_number) { (void)signal_number; stopped = 1; }
static void cleanup(void) {
    if (touch_fd >= 0) { ioctl(touch_fd, UI_DEV_DESTROY); close(touch_fd); }
    if (power_fd >= 0) { ioctl(power_fd, UI_DEV_DESTROY); close(power_fd); }
}
static void fail(const char *what) { perror(what); exit(EXIT_FAILURE); }
static void put(int fd, const void *data, size_t size) {
    ssize_t n;
    do { n = write(fd, data, size); } while (n < 0 && errno == EINTR && !stopped);
    if (n != (ssize_t)size) { if (n >= 0) errno = EIO; fail("uinput write"); }
}
static void setting(int fd, unsigned long code, int value) {
    if (ioctl(fd, code, value) < 0) fail("uinput ioctl");
}
static void pause_ms(int ms) {
    struct timespec delay = {ms / 1000, (ms % 1000) * 1000000L};
    while (!stopped && nanosleep(&delay, &delay) < 0)
        if (errno != EINTR) fail("nanosleep");
    if (stopped) exit(EXIT_FAILURE);
}
static void event(int fd, int type, int code, int value) {
    struct input_event e;
    memset(&e, 0, sizeof e); /* The kernel supplies the event timestamp. */
    e.type = type; e.code = code; e.value = value;
    put(fd, &e, sizeof e);
}
static void sync_frame(int fd) { event(fd, EV_SYN, SYN_REPORT, 0); }
static void create_device(int *fd, const char *name, int touch) {
    *fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
    if (*fd < 0) fail("open /dev/uinput");
    setting(*fd, UI_SET_EVBIT, EV_SYN);
    setting(*fd, UI_SET_EVBIT, EV_KEY);
    setting(*fd, UI_SET_KEYBIT, touch ? BTN_TOUCH : KEY_POWER);
    struct uinput_user_dev device;
    memset(&device, 0, sizeof device);
    snprintf(device.name, sizeof device.name, "%s", name);
    device.id.bustype = BUS_VIRTUAL; device.id.version = 1;
    if (touch) {
        setting(*fd, UI_SET_EVBIT, EV_ABS);
        setting(*fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
        const int codes[] = {ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y};
        const int maxima[] = {9, 65535, 2048, 1536};
        for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
            setting(*fd, UI_SET_ABSBIT, codes[i]); device.absmax[codes[i]] = maxima[i];
        }
    }
    put(*fd, &device, sizeof device);
    if (ioctl(*fd, UI_DEV_CREATE) < 0) fail("UI_DEV_CREATE");
}
static void find_node(int fd, const char *name, char *node, size_t size) {
    char sysname[64], pattern[160];
    if (ioctl(fd, UI_GET_SYSNAME(sizeof sysname), sysname) < 0) fail("UI_GET_SYSNAME");
    snprintf(pattern, sizeof pattern, "/sys/class/input/%s/event*/device/name", sysname);
    for (int attempt = 0; attempt < 500; attempt++) {
        glob_t paths = {0};
        if (glob(pattern, 0, NULL, &paths) == 0) {
            for (size_t i = 0; i < paths.gl_pathc; i++) {
                char found[80] = {0}; FILE *f = fopen(paths.gl_pathv[i], "r");
                if (!f) continue;
                int ok = fgets(found, sizeof found, f) != NULL; fclose(f);
                found[strcspn(found, "\n")] = 0;
                if (!ok || strcmp(found, name)) continue;
                char *event_name = strstr(paths.gl_pathv[i], "/event");
                if (!event_name) continue;
                event_name++;
                snprintf(node, size, "/dev/input/%.*s", (int)strcspn(event_name, "/"), event_name);
                if (access(node, R_OK) == 0) { globfree(&paths); return; }
            }
        }
        globfree(&paths); pause_ms(10);
    }
    errno = ETIMEDOUT; fail("discover input node");
}
/* Do not silently lose the first command while glcube is opening evdev. */
static void wait_reader(const char *node) {
    for (int attempt = 0; attempt < 100; attempt++) {
        glob_t paths = {0}; int found = 0;
        if (glob("/proc/[0-9]*/fd/*", 0, NULL, &paths) == 0) {
            for (size_t i = 0; i < paths.gl_pathc && !found; i++) {
                char target[256]; ssize_t n = readlink(paths.gl_pathv[i], target, sizeof target - 1);
                if (n < 0) continue;
                target[n] = 0; found = strcmp(target, node) == 0;
            }
        }
        globfree(&paths);
        if (found) return;
        pause_ms(100);
    }
    errno = ETIMEDOUT; fail("no evdev reader");
}
static void position(int slot, int x, int y) {
    event(touch_fd, EV_ABS, ABS_MT_SLOT, slot);
    event(touch_fd, EV_ABS, ABS_MT_POSITION_X, native_coordinates ? x : x * 2);
    event(touch_fd, EV_ABS, ABS_MT_POSITION_Y, native_coordinates ? y : (y * 1536 + 300) / 600);
}
static void gesture(const int *p, int fingers, int ms) {
    for (int slot = 0; slot < fingers; slot++) {
        event(touch_fd, EV_ABS, ABS_MT_SLOT, slot);
        tracking_id = tracking_id % 65535 + 1;
        event(touch_fd, EV_ABS, ABS_MT_TRACKING_ID, tracking_id);
        position(slot, p[slot * 2], p[slot * 2 + 1]);
    }
    event(touch_fd, EV_KEY, BTN_TOUCH, 1); sync_frame(touch_fd);
    struct timespec deadline; clock_gettime(CLOCK_MONOTONIC, &deadline);
    for (int elapsed = 0; elapsed < ms && !stopped;) {
        int step = ms - elapsed < 8 ? ms - elapsed : 8; elapsed += step;
        deadline.tv_nsec += step * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
        int error;
        while ((error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL)) == EINTR && !stopped) {}
        if (error && error != EINTR) { errno = error; fail("clock_nanosleep"); }
        for (int slot = 0; slot < fingers; slot++) {
            int x = p[slot * 2], y = p[slot * 2 + 1];
            position(slot, x + (p[fingers * 2 + slot * 2] - x) * elapsed / ms,
                     y + (p[fingers * 2 + slot * 2 + 1] - y) * elapsed / ms);
        }
        sync_frame(touch_fd);
    }
    for (int slot = 0; slot < fingers; slot++) {
        event(touch_fd, EV_ABS, ABS_MT_SLOT, slot);
        event(touch_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
    }
    event(touch_fd, EV_KEY, BTN_TOUCH, 0); sync_frame(touch_fd); pause_ms(24);
}
int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "create")) {
        fprintf(stderr, "usage: touchsim create\ncommands: tap x y; drag x1 y1 x2 y2 ms; hold x y ms; "
                "pinch x1 y1 x2 y2 x3 y3 x4 y4 ms; power; sleep ms; quit\n"
                "TOUCHSIM_NATIVE=1 emits panel pixels instead of scaling to 2048x1536.\n");
        return 2;
    }
    if (sizeof(struct input_event) != 16) { fprintf(stderr, "touchsim requires the 16-byte ARM evdev ABI\n"); return 1; }
    atexit(cleanup);
    struct sigaction action; memset(&action, 0, sizeof action); action.sa_handler = stop;
    sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL); sigaction(SIGHUP, &action, NULL);
    native_coordinates = getenv("TOUCHSIM_NATIVE") && !strcmp(getenv("TOUCHSIM_NATIVE"), "1");
    create_device(&touch_fd, "taq102-sim-touch", 1); create_device(&power_fd, "taq102-sim-power", 0);
    char touch[80], power[80];
    find_node(touch_fd, "taq102-sim-touch", touch, sizeof touch);
    find_node(power_fd, "taq102-sim-power", power, sizeof power);
    printf("TOUCH=%s POWER=%s\n", touch, power); fflush(stdout);
    char line[256];
    while (!stopped && fgets(line, sizeof line, stdin)) {
        if (!strchr(line, '\n') && !feof(stdin)) {
            fprintf(stderr, "touchsim command exceeds 254 bytes or lacks a newline\n"); return 2;
        }
        char command[16]; int p[9], count = 0;
        char *token = strtok(line, " \t\r\n");
        if (!token) continue;
        if (strlen(token) >= sizeof command) goto invalid;
        strcpy(command, token);
        while ((token = strtok(NULL, " \t\r\n"))) {
            char *end; errno = 0; long value = strtol(token, &end, 10);
            if (errno || *end || value < 0 || value > 60000 || count == 9) goto invalid;
            p[count++] = (int)value;
        }
        if (!strcmp(command, "quit") && count == 0) { puts("OK quit"); fflush(stdout); return 0; }
        if (!strcmp(command, "sleep") && count == 1) pause_ms(p[0]);
        else if (!strcmp(command, "power") && count == 0) {
            wait_reader(power); event(power_fd, EV_KEY, KEY_POWER, 1); sync_frame(power_fd);
            pause_ms(80); event(power_fd, EV_KEY, KEY_POWER, 0); sync_frame(power_fd); pause_ms(24);
        } else {
            int fingers = 1, ms;
            if (!strcmp(command, "tap") && count == 2) { ms = 80; p[2] = p[0]; p[3] = p[1]; }
            else if (!strcmp(command, "hold") && count == 3) { ms = p[2]; p[2] = p[0]; p[3] = p[1]; }
            else if (!strcmp(command, "drag") && count == 5) ms = p[4];
            else if (!strcmp(command, "pinch") && count == 9) { fingers = 2; ms = p[8]; }
            else goto invalid;
            if (ms < 8) goto invalid;
            for (int i = 0; i < fingers * 4; i++) if (p[i] > (i % 2 ? 600 : 1024)) goto invalid;
            wait_reader(touch); gesture(p, fingers, ms);
        }
        printf("OK %s\n", command); fflush(stdout); continue;
invalid:
        fprintf(stderr, "invalid touchsim command\n"); return 2;
    }
    return stopped || ferror(stdin) ? 1 : 0;
}
