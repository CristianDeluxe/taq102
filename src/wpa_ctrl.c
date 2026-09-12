#include "wpa_ctrl.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct wpa_ctrl {
    int fd, event_fd;
    char local[108], local_event[108];
    char path[108];
    int generation;
};

// A datagram client needs a bound address of its own for the daemon to
// answer to; wpa_cli uses /tmp/wpa_ctrl_<pid>-<n>, and so does this.
static int dial(const char *path, const char *local) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    struct sockaddr_un mine;
    memset(&mine, 0, sizeof mine);
    mine.sun_family = AF_UNIX;
    snprintf(mine.sun_path, sizeof mine.sun_path, "%s", local);
    unlink(local);
    if (bind(fd, (struct sockaddr *)&mine, sizeof mine) != 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_un daemon;
    memset(&daemon, 0, sizeof daemon);
    daemon.sun_family = AF_UNIX;
    snprintf(daemon.sun_path, sizeof daemon.sun_path, "%s", path);
    if (connect(fd, (struct sockaddr *)&daemon, sizeof daemon) != 0) {
        close(fd);
        unlink(local);
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

static int transact(int fd, const char *cmd, char *buf, size_t cap, int timeout_ms) {
    // A reply that came after its request timed out must not answer this one.
    char stale[64];
    while (recv(fd, stale, sizeof stale, MSG_DONTWAIT | MSG_TRUNC) > 0)
        ;
    size_t len = strlen(cmd);
    if (send(fd, cmd, len, 0) != (ssize_t)len)
        return -1;
    struct pollfd p = { .fd = fd, .events = POLLIN, .revents = 0 };
    if (poll(&p, 1, timeout_ms) <= 0 || !(p.revents & POLLIN)) {
        errno = ETIMEDOUT;
        return -1;
    }
    // One datagram is one reply; a reply that would not fit is refused whole
    // rather than handed back cut, so a caller never parses half a table.
    ssize_t n = recv(fd, buf, cap, MSG_TRUNC);
    if (n < 0)
        return -1;
    if ((size_t)n >= cap) {
        errno = EMSGSIZE;
        return -1;
    }
    buf[n] = '\0';
    return (int)n;
}

struct wpa_ctrl *wpa_ctrl_open(const char *path) {
    if (!path)
        return NULL;
    struct wpa_ctrl *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    snprintf(c->path, sizeof c->path, "%s", path);
    snprintf(c->local, sizeof c->local, "/tmp/dmxdesk-ctrl-%d", (int)getpid());
    snprintf(c->local_event, sizeof c->local_event, "/tmp/dmxdesk-ctrl-%d-ev", (int)getpid());
    c->fd = dial(path, c->local);
    c->event_fd = c->fd >= 0 ? dial(path, c->local_event) : -1;
    if (c->fd < 0 || c->event_fd < 0) {
        fprintf(stderr, "wpa_ctrl: cannot reach %s: %s\n", path, strerror(errno));
        wpa_ctrl_close(c);
        return NULL;
    }
    char reply[16];
    if (transact(c->event_fd, "ATTACH", reply, sizeof reply, 1000) < 0 ||
        strncmp(reply, "OK", 2) != 0) {
        fprintf(stderr, "wpa_ctrl: the daemon at %s refused ATTACH\n", path);
        wpa_ctrl_close(c);
        return NULL;
    }
    return c;
}

int wpa_ctrl_request(struct wpa_ctrl *c, const char *cmd, char *buf, size_t cap,
                     int timeout_ms) {
    if (!c || c->fd < 0 || !cmd || !buf || cap < 2)
        return -1;
    int n = transact(c->fd, cmd, buf, cap, timeout_ms);
    if (n < 0 && errno != EMSGSIZE) {
        // The reply, if it ever comes, goes to an address nobody reads: the
        // request socket is replaced so it cannot answer the next command.
        close(c->fd);
        unlink(c->local);
        c->generation++;
        snprintf(c->local, sizeof c->local, "/tmp/dmxdesk-ctrl-%d-%d", (int)getpid(), c->generation);
        c->fd = dial(c->path, c->local);
    }
    return n;
}

int wpa_ctrl_event_fd(const struct wpa_ctrl *c) { return c ? c->event_fd : -1; }

int wpa_ctrl_event(struct wpa_ctrl *c, char *buf, size_t cap) {
    if (!c || c->event_fd < 0 || !buf || cap < 2)
        return -1;
    ssize_t n = recv(c->event_fd, buf, cap - 1, 0);
    if (n < 0)
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    buf[n] = '\0';
    // "<3>CTRL-EVENT-...": the priority prefix is the daemon's, not ours.
    if (buf[0] == '<') {
        char *end = strchr(buf, '>');
        if (end)
            memmove(buf, end + 1, strlen(end + 1) + 1);
    }
    // The daemon ends its events with a trailing space, and some with a
    // newline; neither is part of the event.
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' '))
        buf[--len] = '\0';
    return 1;
}

void wpa_ctrl_close(struct wpa_ctrl *c) {
    if (!c)
        return;
    if (c->event_fd >= 0) {
        char reply[16];
        transact(c->event_fd, "DETACH", reply, sizeof reply, 200);
        close(c->event_fd);
    }
    if (c->fd >= 0)
        close(c->fd);
    if (c->local[0])
        unlink(c->local);
    if (c->local_event[0])
        unlink(c->local_event);
    free(c);
}
