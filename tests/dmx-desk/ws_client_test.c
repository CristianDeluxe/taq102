// SOURCES: ws_client.c send_queue.c
// Drives the client against a fake QLC+ in the same thread: a listening
// socket whose accept side is serviced between client steps, so no call may
// block or the test hangs and the runner's timeout says so.
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "ws_client.h"

static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// A server frame: FIN + opcode, unmasked, len < 126.
static void server_frame(int fd, int opcode, const void *payload, size_t len) {
    unsigned char h[2] = { (unsigned char)(0x80 | opcode), (unsigned char)len };
    assert(write(fd, h, 2) == 2);
    if (len)
        assert(write(fd, payload, len) == (ssize_t)len);
}

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

static void test_pong_fits(void) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    nonblock(sv[0]);
    nonblock(sv[1]);
    struct ws *ws = ws_adopt(sv[0]);
    assert(ws_state(ws) == WS_OPEN);
    unsigned char body[125];
    memset(body, 'p', sizeof body);
    server_frame(sv[1], 0x9, body, 125);            // the longest legal ping
    char buf[64];
    assert(ws_recv_text(ws, buf, sizeof buf, NULL) == 0);  // consumed, nothing for us
    assert(ws_flush(ws) == 1);
    unsigned char pong[256];
    ssize_t n = read(sv[1], pong, sizeof pong);
    assert(n == 6 + 125);                            // header, mask, 125 bytes
    assert(pong[0] == 0x8A && (pong[1] & 0x7F) == 125 && (pong[1] & 0x80));
    for (int i = 0; i < 125; i++)
        assert(pong[6 + i] == 'p');                  // zero mask
    ws_close(ws);
    close(sv[1]);
}

static void test_frames(void) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    nonblock(sv[0]);
    nonblock(sv[1]);
    struct ws *ws = ws_adopt(sv[0]);
    char buf[128];
    // Two frames in one read.
    server_frame(sv[1], 0x1, "FUNCTION|6|Running", 18);
    server_frame(sv[1], 0x1, "GM_VALUE|128|50%", 16);
    assert(ws_recv_text(ws, buf, sizeof buf, NULL) == 1 && strcmp(buf, "FUNCTION|6|Running") == 0);
    assert(ws_recv_text(ws, buf, sizeof buf, NULL) == 1 && strcmp(buf, "GM_VALUE|128|50%") == 0);
    assert(ws_recv_text(ws, buf, sizeof buf, NULL) == 0);
    // A fragmented text frame is reassembled.
    unsigned char h1[2] = { 0x01, 4 }, h2[2] = { 0x80, 4 };
    assert(write(sv[1], h1, 2) == 2 && write(sv[1], "98|B", 4) == 4);
    assert(ws_recv_text(ws, buf, sizeof buf, NULL) == 0);
    assert(write(sv[1], h2, 2) == 2 && write(sv[1], "UTTO", 4) == 4);
    assert(ws_recv_text(ws, buf, sizeof buf, NULL) == 1 && strcmp(buf, "98|BUTTO") == 0);
    // A 64-bit length is refused and closes the link.
    unsigned char huge[2] = { 0x81, 127 };
    assert(write(sv[1], huge, 2) == 2);
    assert(ws_recv_text(ws, buf, sizeof buf, NULL) == -1);
    ws_close(ws);
    close(sv[1]);
}

// Connect and handshake against a listener serviced by hand. Every client
// step is timed: none may take longer than the work itself, which on loopback
// is well under 50 ms.
static void test_handshake(void) {
    struct sockaddr_in a;
    int ls = listener(&a);
    struct ws *ws = ws_start("127.0.0.1", ntohs(a.sin_port), "/qlcplusWS");
    assert(ws);
    int server = -1;
    char request[1024] = { 0 };
    size_t have = 0;
    for (int i = 0; i < 200 && ws_state(ws) != WS_OPEN; i++) {
        int64_t t0 = now_ms();
        struct pollfd p = { .fd = ws_fd(ws), .events = ws_poll_events(ws), .revents = 0 };
        poll(&p, 1, 10);
        ws_step(ws);
        assert(now_ms() - t0 < 50);
        if (server < 0) {
            server = accept(ls, NULL, NULL);
            if (server >= 0)
                nonblock(server);
        }
        if (server >= 0 && have < sizeof request - 1) {
            ssize_t n = read(server, request + have, sizeof request - 1 - have);
            if (n > 0)
                have += (size_t)n;
            if (strstr(request, "\r\n\r\n")) {
                assert(strstr(request, "GET /qlcplusWS HTTP/1.1"));
                assert(strstr(request, "Upgrade: websocket"));
                const char *reply = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                    "Connection: Upgrade\r\n\r\n";
                assert(write(server, reply, strlen(reply)) == (ssize_t)strlen(reply));
                // The first push rides in the same segment as the headers.
                server_frame(server, 0x1, "FUNCTION|0|Stopped", 18);
                have = sizeof request;               // answered once
            }
        }
    }
    assert(ws_state(ws) == WS_OPEN);
    // The push may share the headers' segment or follow it a moment later on
    // loopback; either way it is the first frame out.
    char buf[64];
    int got = 0;
    for (int i = 0; i < 50 && !got; i++) {
        struct pollfd p = { .fd = ws_fd(ws), .events = POLLIN, .revents = 0 };
        poll(&p, 1, 10);
        got = ws_recv_text(ws, buf, sizeof buf, NULL);
    }
    assert(got == 1 && strcmp(buf, "FUNCTION|0|Stopped") == 0);
    // A send is queued and flushed, masked.
    assert(ws_send_text(ws, "QLC+API|isProjectLoaded") == 0);
    assert(ws_flush(ws) == 1);
    // Loopback delivery is not synchronous: give the segment a moment.
    struct pollfd sp = { .fd = server, .events = POLLIN, .revents = 0 };
    assert(poll(&sp, 1, 500) == 1);
    unsigned char raw[64];
    ssize_t n = read(server, raw, sizeof raw);
    assert(n == 2 + 4 + 23 && raw[0] == 0x81 && (raw[1] & 0x80));
    ws_close(ws);
    close(server);
    close(ls);
}

// A server that answers with anything but 101 fails the handshake with a
// reason; a server that never answers is still not a blocking call.
static void test_refusal(void) {
    struct sockaddr_in a;
    int ls = listener(&a);
    struct ws *ws = ws_start("127.0.0.1", ntohs(a.sin_port), "/qlcplusWS");
    int server = -1;
    for (int i = 0; i < 200 && ws_state(ws) != WS_FAILED; i++) {
        struct pollfd p = { .fd = ws_fd(ws), .events = ws_poll_events(ws), .revents = 0 };
        poll(&p, 1, 10);
        ws_step(ws);
        if (server < 0)
            server = accept(ls, NULL, NULL);
        if (server >= 0 && i == 5) {
            const char *reply = "HTTP/1.1 404 Not Found\r\n\r\n";
            assert(write(server, reply, strlen(reply)) > 0);
        }
    }
    assert(ws_state(ws) == WS_FAILED);
    assert(strstr(ws_reason(ws), "404"));
    ws_close(ws);
    if (server >= 0)
        close(server);
    close(ls);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    test_pong_fits();
    test_frames();
    test_handshake();
    test_refusal();
    printf("ws_client ok\n");
    return 0;
}
