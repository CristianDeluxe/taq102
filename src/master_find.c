#include "master_find.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define REPLY_MAX 2048

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

struct probe {
    int fd;
    uint32_t host;          // host order
    int sent;
    char reply[REPLY_MAX + 1];
    size_t have;
    int done, hit;
};

static int parse_cidr(const char *cidr, uint32_t *addr, int *prefix) {
    char text[64];
    snprintf(text, sizeof text, "%s", cidr ? cidr : "");
    char *slash = strchr(text, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    struct in_addr a;
    if (inet_pton(AF_INET, text, &a) != 1)
        return -1;
    char *end;
    long p = strtol(slash + 1, &end, 10);
    if (end == slash + 1 || *end || p < 8 || p > 30)
        return -1;
    *addr = ntohl(a.s_addr);
    *prefix = (int)p;
    return 0;
}

static void start(struct probe *p, uint32_t host, int port) {
    memset(p, 0, sizeof *p);
    p->host = host;
    p->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (p->fd < 0) {
        p->done = 1;
        return;
    }
    fcntl(p->fd, F_SETFL, fcntl(p->fd, F_GETFL, 0) | O_NONBLOCK);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(host);
    if (connect(p->fd, (struct sockaddr *)&a, sizeof a) != 0 && errno != EINPROGRESS) {
        close(p->fd);
        p->fd = -1;
        p->done = 1;
    }
}

static void step(struct probe *p) {
    if (p->done)
        return;
    if (!p->sent) {
        int err = 0;
        socklen_t len = sizeof err;
        struct pollfd w = { .fd = p->fd, .events = POLLOUT, .revents = 0 };
        if (poll(&w, 1, 0) <= 0 || !(w.revents & POLLOUT))
            return;
        if (getsockopt(p->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            p->done = 1;
            return;
        }
        static const char request[] = "GET / HTTP/1.0\r\n\r\n";
        if (send(p->fd, request, sizeof request - 1, 0) != (ssize_t)(sizeof request - 1)) {
            p->done = 1;
            return;
        }
        p->sent = 1;
        return;
    }
    ssize_t n = recv(p->fd, p->reply + p->have, REPLY_MAX - p->have, 0);
    if (n > 0) {
        p->have += (size_t)n;
        p->reply[p->have] = '\0';
        if (strstr(p->reply, "QLC+")) {
            p->hit = 1;
            p->done = 1;
        } else if (p->have >= REPLY_MAX) {
            p->done = 1;
        }
        return;
    }
    if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        p->done = 1;
}

static void finish(struct probe *p) {
    if (p->fd >= 0)
        close(p->fd);
    p->fd = -1;
    p->done = 1;
}

int master_find_run(const char *cidr, int port, FILE *out) {
    uint32_t self;
    int prefix;
    if (parse_cidr(cidr, &self, &prefix) != 0 || port < 1 || port > 65535)
        return -1;
    uint32_t mask = prefix == 0 ? 0 : 0xFFFFFFFFu << (32 - prefix);
    uint32_t network = self & mask, broadcast = network | ~mask;
    uint32_t first = network + 1, last = broadcast - 1;
    int partial = 0;
    if (last - first + 1 > MASTER_FIND_MAX_HOSTS) {
        last = first + MASTER_FIND_MAX_HOSTS - 1;
        partial = 1;
    }
    int found = 0;
    struct probe batch[MASTER_FIND_BATCH];
    for (uint32_t base = first; base <= last; base += MASTER_FIND_BATCH) {
        int count = 0;
        for (uint32_t h = base; h <= last && count < MASTER_FIND_BATCH; h++) {
            if (h == self)
                continue;
            start(&batch[count++], h, port);
        }
        int64_t deadline = now_ms() + MASTER_FIND_BATCH_MS;
        for (;;) {
            int live = 0;
            for (int i = 0; i < count; i++) {
                step(&batch[i]);
                if (!batch[i].done)
                    live++;
            }
            if (!live || now_ms() >= deadline)
                break;
            struct pollfd fds[MASTER_FIND_BATCH];
            int n = 0;
            for (int i = 0; i < count; i++) {
                if (batch[i].done)
                    continue;
                fds[n].fd = batch[i].fd;
                fds[n].events = batch[i].sent ? POLLIN : POLLOUT;
                fds[n].revents = 0;
                n++;
            }
            int wait = (int)(deadline - now_ms());
            poll(fds, (nfds_t)n, wait > 20 ? 20 : (wait > 0 ? wait : 0));
        }
        for (int i = 0; i < count; i++) {
            if (batch[i].hit) {
                struct in_addr a = { htonl(batch[i].host) };
                fprintf(out, "%s\n", inet_ntoa(a));
                found++;
            }
            finish(&batch[i]);
        }
    }
    if (partial)
        fprintf(out, "partial\n");
    fflush(out);
    return found;
}
