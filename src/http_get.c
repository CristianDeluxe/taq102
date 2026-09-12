#include "http_get.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int dial(const char *host, int port) {
    char service[16];
    snprintf(service, sizeof service, "%d", port);
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *list = NULL;
    if (getaddrinfo(host, service, &hints, &list) != 0)
        return -1;
    int fd = -1;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    return fd;
}

static ssize_t read_some(int fd, void *buf, size_t cap, int timeout_ms) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    if (poll(&p, 1, timeout_ms) <= 0)
        return -1;
    return recv(fd, buf, cap, 0);
}

int http_get(const char *host, int port, const char *path, int timeout_ms,
             size_t limit, char **body, size_t *len) {
    if (!host || !path || !body || !len)
        return -1;
    *body = NULL;
    *len = 0;

    int fd = dial(host, port);
    if (fd < 0) {
        fprintf(stderr, "http: cannot reach %s:%d\n", host, port);
        return -1;
    }

    char request[512];
    int n = snprintf(request, sizeof request,
                     "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n"
                     "Accept: application/json\r\n\r\n", path, host, port);
    if (n < 0 || (size_t)n >= sizeof request || send(fd, request, (size_t)n, 0) != n) {
        close(fd);
        return -1;
    }

    // Headers first, into a fixed buffer: a server that sends more header than
    // this is not the one we came for.
    char head[4096];
    size_t have = 0;
    char *split = NULL;
    while (have < sizeof head - 1) {
        ssize_t got = read_some(fd, head + have, sizeof head - 1 - have, timeout_ms);
        if (got <= 0)
            break;
        have += (size_t)got;
        head[have] = '\0';
        split = strstr(head, "\r\n\r\n");
        if (split)
            break;
    }
    if (!split) {
        fprintf(stderr, "http: no headers from %s%s\n", host, path);
        close(fd);
        return -1;
    }
    if (strncmp(head, "HTTP/1.1 200", 12) != 0 && strncmp(head, "HTTP/1.0 200", 12) != 0) {
        char *end = strchr(head, '\r');
        if (end)
            *end = '\0';
        fprintf(stderr, "http: %s\n", head);
        close(fd);
        return -1;
    }

    size_t declared = 0;
    const char *cl = strcasestr(head, "content-length:");
    if (cl)
        declared = strtoul(cl + 15, NULL, 10);
    if (declared > limit) {
        fprintf(stderr, "http: %zu bytes is more than this desk reads\n", declared);
        close(fd);
        return -1;
    }

    size_t capacity = declared ? declared + 1 : limit + 1;
    char *out = malloc(capacity);
    if (!out) {
        close(fd);
        return -1;
    }
    size_t body_have = have - (size_t)(split + 4 - head);
    memcpy(out, split + 4, body_have);

    while (declared ? body_have < declared : body_have < limit) {
        ssize_t got = read_some(fd, out + body_have, capacity - 1 - body_have,
                                timeout_ms);
        if (got <= 0)
            break;
        body_have += (size_t)got;
    }
    close(fd);

    if (declared && body_have != declared) {
        fprintf(stderr, "http: %zu bytes of a declared %zu\n", body_have, declared);
        free(out);
        return -1;
    }
    out[body_have] = '\0';
    *body = out;
    *len = body_have;
    return 0;
}
