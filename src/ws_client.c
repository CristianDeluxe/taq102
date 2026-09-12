#include "ws_client.h"

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "send_queue.h"

// A command is short and a push is short. 64 KB is already far past anything
// this protocol sends, and it is the point at which a peer is either broken or
// not QLC+.
#define WS_MAX_FRAME (64 * 1024)
#define WS_MAX_SEND 1024
#define WS_BUF (128 * 1024)

struct ws {
    int fd;
    enum ws_state state;
    char reason[96];
    struct send_queue out;
    unsigned char in[WS_BUF];
    size_t have;
    unsigned char assembled[WS_MAX_FRAME];
    size_t assembled_len;
    int assembling_text;
};

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void fail(struct ws *ws, const char *reason) {
    ws->state = WS_FAILED;
    snprintf(ws->reason, sizeof ws->reason, "%s", reason);
}

static struct ws *fresh(int fd) {
    struct ws *ws = calloc(1, sizeof *ws);
    if (!ws)
        return NULL;
    ws->fd = fd;
    send_queue_init(&ws->out);
    return ws;
}

struct ws *ws_adopt(int fd) {
    struct ws *ws = fresh(fd);
    if (ws)
        ws->state = WS_OPEN;
    return ws;
}

// The key is not a secret and the accept hash is not checked: this is a
// request to a server on the show's own network, and a wrong answer shows up
// immediately as a failed status line rather than as a security boundary.
struct ws *ws_start(const char *host, int port, const char *path) {
    char service[16];
    snprintf(service, sizeof service, "%d", port);
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *list = NULL;
    if (getaddrinfo(host, service, &hints, &list) != 0)
        return NULL;

    // The socket goes non-blocking before connect, so connect returns at once
    // with EINPROGRESS and the handshake's deadline is the caller's, not the
    // kernel's minutes-long SYN retry.
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
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    struct ws *ws = fresh(fd);
    if (!ws) {
        close(fd);
        return NULL;
    }
    ws->state = WS_CONNECTING;
    char request[512];
    int n = snprintf(request, sizeof request,
                     "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                     "Sec-WebSocket-Version: 13\r\n\r\n", path, host, port);
    if (n < 0 || (size_t)n >= sizeof request ||
        send_queue_push(&ws->out, request, (size_t)n) != 0) {
        ws_close(ws);
        return NULL;
    }
    return ws;
}

static int pump(struct ws *ws) {
    if (ws->have >= sizeof ws->in)
        return -1;
    ssize_t n = recv(ws->fd, ws->in + ws->have, sizeof ws->in - ws->have, 0);
    if (n > 0) {
        ws->have += (size_t)n;
        return 1;
    }
    if (n == 0)
        return -1;                       // the peer closed
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return 0;
    return -1;
}

// The reply's headers and the first frames arrive in the same read, so the
// handshake reads into the connection's own buffer and leaves whatever
// followed the blank line in it. A local buffer here would silently drop the
// first push of every connection.
static void step_handshake(struct ws *ws) {
    if (send_queue_flush(&ws->out, ws->fd) < 0) {
        fail(ws, "socket closed during the handshake");
        return;
    }
    if (pump(ws) < 0) {
        fail(ws, "socket closed during the handshake");
        return;
    }
    unsigned char *end = memmem(ws->in, ws->have, "\r\n\r\n", 4);
    if (!end) {
        if (ws->have >= sizeof ws->in)
            fail(ws, "no headers in the reply");
        return;
    }
    if (ws->have < 12 || memcmp(ws->in, "HTTP/1.1 101", 12) != 0) {
        char line[80];
        size_t n = ws->have < sizeof line - 1 ? ws->have : sizeof line - 1;
        memcpy(line, ws->in, n);
        line[n] = '\0';
        char *cr = strchr(line, '\r');
        if (cr)
            *cr = '\0';
        char reason[96];
        snprintf(reason, sizeof reason, "server said %s", line);
        fail(ws, reason);
        return;
    }
    size_t consumed = (size_t)(end - ws->in) + 4;
    memmove(ws->in, ws->in + consumed, ws->have - consumed);
    ws->have -= consumed;
    ws->state = WS_OPEN;
}

enum ws_state ws_step(struct ws *ws) {
    if (!ws)
        return WS_FAILED;
    switch (ws->state) {
    case WS_CONNECTING: {
        int err = 0;
        socklen_t len = sizeof err;
        if (getsockopt(ws->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
            err = errno;
        if (err != 0) {
            fail(ws, strerror(err));
            break;
        }
        // A pending connect reports no error and takes no bytes yet; a
        // completed one accepts the request.
        struct pollfd p = { .fd = ws->fd, .events = POLLOUT, .revents = 0 };
        if (poll(&p, 1, 0) > 0 && (p.revents & POLLOUT)) {
            ws->state = WS_HANDSHAKING;
            step_handshake(ws);
        }
        break;
    }
    case WS_HANDSHAKING:
        step_handshake(ws);
        break;
    case WS_OPEN:
        if (send_queue_flush(&ws->out, ws->fd) < 0)
            fail(ws, "socket closed");
        break;
    case WS_FAILED:
        break;
    }
    return ws->state;
}

enum ws_state ws_state(const struct ws *ws) { return ws ? ws->state : WS_FAILED; }
const char *ws_reason(const struct ws *ws) { return ws ? ws->reason : "no socket"; }

short ws_poll_events(const struct ws *ws) {
    if (!ws)
        return 0;
    short events = 0;
    if (ws->state == WS_CONNECTING || !send_queue_empty(&ws->out))
        events |= POLLOUT;
    if (ws->state == WS_HANDSHAKING || ws->state == WS_OPEN)
        events |= POLLIN;
    return events;
}

int ws_fd(const struct ws *ws) { return ws ? ws->fd : -1; }

int ws_send_text(struct ws *ws, const char *text) {
    if (!ws || ws->fd < 0 || ws->state != WS_OPEN || !text)
        return -1;
    size_t len = strlen(text);
    if (len == 0 || len > WS_MAX_SEND)
        return -1;

    unsigned char frame[14 + WS_MAX_SEND];
    size_t at = 0;
    frame[at++] = 0x81;                  // FIN, text
    // Client frames are always masked. The mask is not a secret either; it
    // exists so proxies cannot be fooled into seeing a request.
    unsigned char mask[4];
    for (int i = 0; i < 4; i++)
        mask[i] = (unsigned char)(rand() & 0xFF);
    if (len < 126) {
        frame[at++] = (unsigned char)(0x80 | len);
    } else {
        frame[at++] = 0x80 | 126;
        frame[at++] = (unsigned char)(len >> 8);
        frame[at++] = (unsigned char)(len & 0xFF);
    }
    memcpy(frame + at, mask, 4);
    at += 4;
    for (size_t i = 0; i < len; i++)
        frame[at + i] = (unsigned char)text[i] ^ mask[i % 4];
    at += len;
    return send_queue_push(&ws->out, frame, at);
}

int ws_flush(struct ws *ws) {
    if (!ws || ws->fd < 0)
        return -1;
    return send_queue_flush(&ws->out, ws->fd);
}

static void consume(struct ws *ws, size_t bytes) {
    memmove(ws->in, ws->in + bytes, ws->have - bytes);
    ws->have -= bytes;
}

int ws_recv_text(struct ws *ws, char *buf, size_t cap) {
    if (!ws || ws->fd < 0 || ws->state != WS_OPEN || !buf || cap == 0)
        return -1;
    for (;;) {
        // A frame: two header bytes, an optional extended length, no mask
        // (servers do not mask), then the payload.
        if (ws->have >= 2) {
            unsigned char first = ws->in[0], second = ws->in[1];
            int fin = first & 0x80;
            int opcode = first & 0x0F;
            int masked = second & 0x80;
            size_t len = second & 0x7F;
            size_t offset = 2;
            if (len == 126) {
                if (ws->have < 4)
                    goto want_more;
                len = ((size_t)ws->in[2] << 8) | ws->in[3];
                offset = 4;
            } else if (len == 127) {
                return -1;               // 64-bit lengths are not this protocol
            }
            if (masked)
                offset += 4;             // a masked server frame is broken, but
                                         // skip its mask rather than misread it
            if (len > WS_MAX_FRAME)
                return -1;
            if (ws->have < offset + len)
                goto want_more;

            const unsigned char *payload = ws->in + offset;
            if (opcode == 0x8) {         // close
                consume(ws, offset + len);
                return -1;
            }
            if (opcode == 0x9) {         // ping: answer with the same body
                // Six header bytes and up to 125 of payload: the buffer was
                // two bytes short of that once, and a 125-byte ping wrote
                // past it.
                unsigned char pong[6 + 125];
                if (len <= 125) {
                    pong[0] = 0x8A;
                    pong[1] = (unsigned char)(0x80 | len);
                    memset(pong + 2, 0, 4);
                    memcpy(pong + 6, payload, len);
                    if (send_queue_push(&ws->out, pong, 6 + len) != 0)
                        return -1;
                }
                consume(ws, offset + len);
                continue;
            }
            if (opcode == 0x1 || opcode == 0x0) {
                if (opcode == 0x1) {
                    ws->assembled_len = 0;
                    ws->assembling_text = 1;
                }
                if (ws->assembling_text) {
                    if (ws->assembled_len + len > WS_MAX_FRAME)
                        return -1;
                    memcpy(ws->assembled + ws->assembled_len, payload, len);
                    ws->assembled_len += len;
                }
                int complete = fin && ws->assembling_text;
                consume(ws, offset + len);
                if (!complete)
                    continue;
                size_t n = ws->assembled_len < cap - 1 ? ws->assembled_len : cap - 1;
                memcpy(buf, ws->assembled, n);
                buf[n] = '\0';
                ws->assembled_len = 0;
                ws->assembling_text = 0;
                return 1;
            }
            // Binary or anything else: this protocol has none, drop it.
            consume(ws, offset + len);
            continue;
        }
want_more:
        switch (pump(ws)) {
        case 1:  continue;
        case 0:  return 0;
        default: return -1;
        }
    }
}

void ws_close(struct ws *ws) {
    if (!ws)
        return;
    if (ws->fd >= 0)
        close(ws->fd);
    free(ws);
}
