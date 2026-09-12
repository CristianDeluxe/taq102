#include "http_fetch.h"

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct http_fetch {
    int fd;
    enum http_fetch_state state;
    char request[512];
    size_t request_len, request_sent;
    // Headers first, into a fixed buffer: a server that sends more header
    // than this is not the one we came for.
    char head[4096];
    size_t head_have;
    char *body;
    size_t body_have, body_cap, declared, limit;
    int have_declared;
    char reason[96];
};

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static enum http_fetch_state fail(struct http_fetch *f, const char *reason) {
    f->state = HTTP_FETCH_FAILED;
    snprintf(f->reason, sizeof f->reason, "%s", reason);
    return f->state;
}

struct http_fetch *http_fetch_start(const char *host, int port, const char *path,
                                    size_t limit) {
    if (!host || !path)
        return NULL;
    char service[16];
    snprintf(service, sizeof service, "%d", port);
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *list = NULL;
    if (getaddrinfo(host, service, &hints, &list) != 0)
        return NULL;
    int fd = -1;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0)
            continue;
        if (set_nonblocking(fd) == 0 &&
            (connect(fd, a->ai_addr, a->ai_addrlen) == 0 || errno == EINPROGRESS))
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    if (fd < 0)
        return NULL;

    struct http_fetch *f = calloc(1, sizeof *f);
    if (!f) {
        close(fd);
        return NULL;
    }
    f->fd = fd;
    f->limit = limit;
    f->state = HTTP_FETCH_CONNECTING;
    int n = snprintf(f->request, sizeof f->request,
                     "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n"
                     "Accept: application/json\r\n\r\n", path, host, port);
    if (n < 0 || (size_t)n >= sizeof f->request) {
        http_fetch_free(f);
        return NULL;
    }
    f->request_len = (size_t)n;
    return f;
}

int http_fetch_fd(const struct http_fetch *f) { return f ? f->fd : -1; }

short http_fetch_poll_events(const struct http_fetch *f) {
    if (!f)
        return 0;
    if (f->state == HTTP_FETCH_CONNECTING || f->request_sent < f->request_len)
        return POLLOUT;
    return f->state == HTTP_FETCH_READING ? POLLIN : 0;
}

// Sends what the socket takes of the request. 1 when all of it has gone.
static int send_request(struct http_fetch *f) {
    while (f->request_sent < f->request_len) {
        ssize_t n = send(f->fd, f->request + f->request_sent,
                         f->request_len - f->request_sent, MSG_NOSIGNAL);
        if (n > 0) {
            f->request_sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        return -1;
    }
    return 1;
}

// Reads the headers; on the blank line checks the status, takes the declared
// length, sizes the body and moves what followed the headers into it.
static enum http_fetch_state read_headers(struct http_fetch *f) {
    for (;;) {
        char *split = f->head_have ? memmem(f->head, f->head_have, "\r\n\r\n", 4) : NULL;
        if (split) {
            f->head[f->head_have] = '\0';
            if (strncmp(f->head, "HTTP/1.1 200", 12) != 0 &&
                strncmp(f->head, "HTTP/1.0 200", 12) != 0) {
                char line[80];
                snprintf(line, sizeof line, "%.79s", f->head);
                char *cr = strchr(line, '\r');
                if (cr)
                    *cr = '\0';
                return fail(f, line);
            }
            *split = '\0';
            const char *cl = strcasestr(f->head, "content-length:");
            if (cl) {
                f->declared = strtoul(cl + 15, NULL, 10);
                f->have_declared = 1;
                if (f->declared > f->limit) {
                    char reason[96];
                    snprintf(reason, sizeof reason,
                             "%zu bytes is more than this desk reads", f->declared);
                    return fail(f, reason);
                }
            }
            f->body_cap = (f->have_declared ? f->declared : f->limit) + 1;
            f->body = malloc(f->body_cap);
            if (!f->body)
                return fail(f, "out of memory");
            size_t after = f->head_have - (size_t)(split + 4 - f->head);
            if (after > f->body_cap - 1)
                after = f->body_cap - 1;
            memcpy(f->body, split + 4, after);
            f->body_have = after;
            return f->state;
        }
        if (f->head_have >= sizeof f->head - 1)
            return fail(f, "no headers in the reply");
        ssize_t n = recv(f->fd, f->head + f->head_have, sizeof f->head - 1 - f->head_have, 0);
        if (n > 0) {
            f->head_have += (size_t)n;
            continue;
        }
        if (n == 0)
            return fail(f, "closed before the headers");
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return f->state;
        return fail(f, strerror(errno));
    }
}

static enum http_fetch_state read_body(struct http_fetch *f) {
    for (;;) {
        if (f->have_declared && f->body_have >= f->declared) {
            f->body[f->body_have] = '\0';
            f->state = HTTP_FETCH_DONE;
            return f->state;
        }
        if (f->body_have >= f->body_cap - 1) {
            char reason[96];
            snprintf(reason, sizeof reason, "%zu bytes is more than this desk reads",
                     f->body_cap - 1);
            return fail(f, reason);
        }
        ssize_t n = recv(f->fd, f->body + f->body_have, f->body_cap - 1 - f->body_have, 0);
        if (n > 0) {
            f->body_have += (size_t)n;
            continue;
        }
        if (n == 0) {
            if (f->have_declared && f->body_have != f->declared) {
                char reason[96];
                snprintf(reason, sizeof reason, "%zu bytes of a declared %zu",
                         f->body_have, f->declared);
                return fail(f, reason);
            }
            f->body[f->body_have] = '\0';
            f->state = HTTP_FETCH_DONE;
            return f->state;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return f->state;
        return fail(f, strerror(errno));
    }
}

enum http_fetch_state http_fetch_step(struct http_fetch *f) {
    if (!f)
        return HTTP_FETCH_FAILED;
    switch (f->state) {
    case HTTP_FETCH_CONNECTING: {
        int err = 0;
        socklen_t len = sizeof err;
        if (getsockopt(f->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
            err = errno;
        if (err != 0)
            return fail(f, strerror(err));
        struct pollfd p = { .fd = f->fd, .events = POLLOUT, .revents = 0 };
        if (poll(&p, 1, 0) <= 0 || !(p.revents & POLLOUT))
            return f->state;
        int sent = send_request(f);
        if (sent < 0)
            return fail(f, strerror(errno));
        if (sent == 1)
            f->state = HTTP_FETCH_READING;
        return f->state;
    }
    case HTTP_FETCH_READING:
        if (f->request_sent < f->request_len) {
            int sent = send_request(f);
            if (sent < 0)
                return fail(f, strerror(errno));
            if (sent == 0)
                return f->state;
        }
        if (!f->body) {
            enum http_fetch_state st = read_headers(f);
            if (st != HTTP_FETCH_READING || !f->body)
                return st;
        }
        return read_body(f);
    case HTTP_FETCH_DONE:
    case HTTP_FETCH_FAILED:
        return f->state;
    }
    return f->state;
}

const char *http_fetch_reason(const struct http_fetch *f) { return f ? f->reason : "no fetch"; }

const char *http_fetch_body(const struct http_fetch *f, size_t *len) {
    if (!f || f->state != HTTP_FETCH_DONE) {
        if (len)
            *len = 0;
        return NULL;
    }
    if (len)
        *len = f->body_have;
    return f->body;
}

void http_fetch_free(struct http_fetch *f) {
    if (!f)
        return;
    if (f->fd >= 0)
        close(f->fd);
    free(f->body);
    free(f);
}
