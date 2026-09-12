// SOURCES: wpa_ctrl.c
// A fake wpa_supplicant on a unix datagram socket, serviced in the same
// thread: it answers PING and STATUS, acknowledges ATTACH, and after SCAN
// pushes the scan-results event to whoever attached. No call may block
// longer than its timeout.
#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "wpa_ctrl.h"

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

struct fake {
    int fd;
    char path[108];
    struct sockaddr_un attached;
    int has_attached;
    int scans;
};

static void fake_open(struct fake *k) {
    memset(k, 0, sizeof *k);
    snprintf(k->path, sizeof k->path, "/tmp/dmxdesk-fake-wpa-%d", (int)getpid());
    unlink(k->path);
    k->fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    assert(k->fd >= 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, k->path);
    assert(bind(k->fd, (struct sockaddr *)&a, sizeof a) == 0);
}

// Answers every datagram waiting, like the daemon would; `silent` drops a
// command instead, to exercise the client's timeout.
static void fake_service(struct fake *k, const char *silent) {
    for (;;) {
        char cmd[256];
        struct sockaddr_un from;
        socklen_t flen = sizeof from;
        struct pollfd p = { .fd = k->fd, .events = POLLIN, .revents = 0 };
        if (poll(&p, 1, 5) <= 0)
            return;
        ssize_t n = recvfrom(k->fd, cmd, sizeof cmd - 1, 0, (struct sockaddr *)&from, &flen);
        if (n <= 0)
            return;
        cmd[n] = '\0';
        const char *reply = NULL;
        if (silent && strcmp(cmd, silent) == 0)
            continue;
        if (strcmp(cmd, "ATTACH") == 0) {
            k->attached = from;
            k->has_attached = 1;
            reply = "OK\n";
        } else if (strcmp(cmd, "DETACH") == 0) {
            reply = "OK\n";
        } else if (strcmp(cmd, "PING") == 0) {
            reply = "PONG\n";
        } else if (strcmp(cmd, "STATUS") == 0) {
            reply = "bssid=00:00:5e:00:53:01\nfreq=2437\nssid=TestNet\nid=0\nwpa_state=COMPLETED\n"
                    "ip_address=192.168.1.71\n";
        } else if (strcmp(cmd, "SCAN") == 0) {
            k->scans++;
            reply = "OK\n";
            sendto(k->fd, reply, strlen(reply), 0, (struct sockaddr *)&from, flen);
            if (k->has_attached) {
                const char *ev = "<3>CTRL-EVENT-SCAN-RESULTS ";
                sendto(k->fd, ev, strlen(ev), 0, (struct sockaddr *)&k->attached, sizeof k->attached);
            }
            continue;
        } else if (strcmp(cmd, "BIG") == 0) {
            static char big[4096];
            memset(big, 'x', sizeof big - 1);
            big[sizeof big - 1] = '\0';
            reply = big;
        } else {
            reply = "FAIL\n";
        }
        sendto(k->fd, reply, strlen(reply), 0, (struct sockaddr *)&from, flen);
    }
}

// The fake is serviced by a forked helper, so a request here is answered
// while this process waits in the client's own poll.
static int ask(struct wpa_ctrl *c, const char *cmd, char *buf, size_t cap) {
    return wpa_ctrl_request(c, cmd, buf, cap, 300);
}

int main(void) {
    struct fake k;
    fake_open(&k);
    // The client's open ATTACHes and waits for OK: fork a helper that
    // services the fake while this process opens and asks.
    pid_t helper = fork();
    assert(helper >= 0);
    if (helper == 0) {
        for (int i = 0; i < 600; i++)
            fake_service(&k, "SLOW");
        _exit(0);
    }
    struct wpa_ctrl *c = wpa_ctrl_open(k.path);
    assert(c);
    char buf[512];
    assert(ask(c, "PING", buf, sizeof buf) == 5 && strcmp(buf, "PONG\n") == 0);
    assert(ask(c, "STATUS", buf, sizeof buf) > 0 && strstr(buf, "ssid=TestNet"));
    // A command the daemon never answers times out in about its timeout.
    int64_t t0 = now_ms();
    assert(wpa_ctrl_request(c, "SLOW", buf, sizeof buf, 200) == -1);
    int64_t took = now_ms() - t0;
    assert(took >= 180 && took < 800);
    // A reply that would not fit is refused, not cut.
    char small[64];
    assert(wpa_ctrl_request(c, "BIG", small, sizeof small, 300) == -1);
    // A scan ends on the event, on the attached socket, with the prefix off.
    assert(wpa_ctrl_request(c, "SCAN", buf, sizeof buf, 300) == 3);
    int got = 0;
    for (int i = 0; i < 100 && !got; i++) {
        struct pollfd p = { .fd = wpa_ctrl_event_fd(c), .events = POLLIN, .revents = 0 };
        poll(&p, 1, 10);
        got = wpa_ctrl_event(c, buf, sizeof buf);
    }
    assert(got == 1 && strcmp(buf, "CTRL-EVENT-SCAN-RESULTS") == 0);
    assert(wpa_ctrl_event(c, buf, sizeof buf) == 0);
    wpa_ctrl_close(c);
    kill(helper, 9);
    close(k.fd);
    unlink(k.path);
    printf("wpa_ctrl ok\n");
    return 0;
}
