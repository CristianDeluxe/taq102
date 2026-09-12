// SOURCES: http_fetch.c
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "http_fetch.h"

static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

static int listener(struct sockaddr_in *a) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    memset(a, 0, sizeof *a);
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(ls, (struct sockaddr *)a, sizeof *a) == 0);
    socklen_t alen = sizeof *a;
    assert(getsockname(ls, (struct sockaddr *)a, &alen) == 0);
    assert(listen(ls, 1) == 0);
    nonblock(ls);
    return ls;
}

// Runs the fetch against a server whose reply is written `piece` bytes per
// client step, so the body always arrives split, and closed at the end.
static enum http_fetch_state run(const char *reply, size_t reply_len, size_t piece,
                                 size_t limit, struct http_fetch **out) {
    struct sockaddr_in a;
    int ls = listener(&a);
    struct http_fetch *f = http_fetch_start("127.0.0.1", ntohs(a.sin_port), "/vc.json", limit);
    assert(f);
    int server = -1;
    size_t sent = 0;
    char request[512] = { 0 };
    size_t have = 0;
    int seen_request = 0;
    enum http_fetch_state st = HTTP_FETCH_CONNECTING;
    for (int i = 0; i < 500; i++) {
        struct pollfd p = { .fd = http_fetch_fd(f), .events = http_fetch_poll_events(f), .revents = 0 };
        poll(&p, 1, 5);
        st = http_fetch_step(f);
        if (st == HTTP_FETCH_DONE || st == HTTP_FETCH_FAILED)
            break;
        if (server < 0) {
            server = accept(ls, NULL, NULL);
            if (server >= 0)
                nonblock(server);
        }
        if (server >= 0 && !seen_request) {
            ssize_t n = read(server, request + have, sizeof request - 1 - have);
            if (n > 0)
                have += (size_t)n;
            if (strstr(request, "\r\n\r\n")) {
                assert(strstr(request, "GET /vc.json HTTP/1.1"));
                seen_request = 1;
            }
        } else if (server >= 0 && sent < reply_len) {
            size_t n = reply_len - sent < piece ? reply_len - sent : piece;
            assert(write(server, reply + sent, n) == (ssize_t)n);
            sent += n;
            if (sent == reply_len) {
                close(server);
                server = -2;
            }
        }
    }
    if (server >= 0)
        close(server);
    close(ls);
    *out = f;
    return st;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    struct http_fetch *f;
    size_t len;

    const char ok[] = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nContent-Type: text/html\r\n\r\nhello world";
    assert(run(ok, sizeof ok - 1, 7, 1024, &f) == HTTP_FETCH_DONE);
    assert(strcmp(http_fetch_body(f, &len), "hello world") == 0 && len == 11);
    http_fetch_free(f);

    // No Content-Length: the body is whatever arrived before the close.
    const char eof[] = "HTTP/1.0 200 OK\r\n\r\n{\"a\":1}";
    assert(run(eof, sizeof eof - 1, 5, 1024, &f) == HTTP_FETCH_DONE);
    assert(strcmp(http_fetch_body(f, &len), "{\"a\":1}") == 0);
    http_fetch_free(f);

    // A declared length past the limit fails before the body is read.
    const char big[] = "HTTP/1.1 200 OK\r\nContent-Length: 5000\r\n\r\nxxxxx";
    assert(run(big, sizeof big - 1, 100, 1024, &f) == HTTP_FETCH_FAILED);
    assert(strstr(http_fetch_reason(f), "5000"));
    http_fetch_free(f);

    // A short body against its declared length fails rather than handing a
    // truncated document to the parser.
    const char cut[] = "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\nonly ten b";
    assert(run(cut, sizeof cut - 1, 100, 1024, &f) == HTTP_FETCH_FAILED);
    http_fetch_free(f);

    // A non-200 is a failure carrying the status line.
    const char nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    assert(run(nf, sizeof nf - 1, 100, 1024, &f) == HTTP_FETCH_FAILED);
    assert(strstr(http_fetch_reason(f), "404"));
    http_fetch_free(f);

    // A dead port fails at the connect, without blocking.
    f = http_fetch_start("127.0.0.1", 1, "/vc.json", 1024);
    assert(f);
    enum http_fetch_state st = HTTP_FETCH_CONNECTING;
    for (int i = 0; i < 200 && st == HTTP_FETCH_CONNECTING; i++) {
        struct pollfd p = { .fd = http_fetch_fd(f), .events = http_fetch_poll_events(f), .revents = 0 };
        poll(&p, 1, 5);
        st = http_fetch_step(f);
    }
    assert(st == HTTP_FETCH_FAILED);
    http_fetch_free(f);

    printf("http_fetch ok\n");
    return 0;
}
