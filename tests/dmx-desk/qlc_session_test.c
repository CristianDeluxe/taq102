// SOURCES: qlc_session.c ws_client.c send_queue.c http_fetch.c vcjson.c
// A fake QLC+ in the same thread: one listener answering both the socket
// upgrade and the snapshot request, serviced between session steps under a
// fake clock, so the deadlines are exact and nothing can block.
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "qlc_session.h"
#include "desk_heartbeat_ms.h"
#include "vcjson.h"

static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    assert(fread(b, 1, (size_t)n, f) == (size_t)n);
    b[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return b;
}

struct fake {
    int ls, port;
    int ws_fd, http_fd;         // accepted sockets, -1 until they arrive
    int pending[4];             // accepted, request line not yet seen
    int npending;
    char buf[4096];
    size_t have;
    const char *snapshot;
    size_t snapshot_len;
    int answer_snapshot;        // 0: hang the fetch forever
    int refuse_ws;              // 1: answer 404 to the upgrade
    int pings_seen;
};

static void fake_init(struct fake *k) {
    memset(k, 0, sizeof *k);
    k->ws_fd = k->http_fd = -1;
    k->ls = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    assert(bind(k->ls, (struct sockaddr *)&a, sizeof a) == 0);
    socklen_t alen = sizeof a;
    assert(getsockname(k->ls, (struct sockaddr *)&a, &alen) == 0);
    assert(listen(k->ls, 4) == 0);
    nonblock(k->ls);
    k->port = ntohs(a.sin_port);
    k->answer_snapshot = 1;
}

static void fake_close(struct fake *k) {
    for (int i = 0; i < k->npending; i++) close(k->pending[i]);
    if (k->ws_fd >= 0) close(k->ws_fd);
    if (k->http_fd >= 0) close(k->http_fd);
    close(k->ls);
}

static void server_text(int fd, const char *s) {
    unsigned char h[2] = { 0x81, (unsigned char)strlen(s) };
    assert(write(fd, h, 2) == 2 && write(fd, s, strlen(s)) == (ssize_t)strlen(s));
}

// One round of servicing: accept anything pending, tell the two connections
// apart by their request line, answer whichever request has arrived.
static void fake_service(struct fake *k) {
    int c;
    while ((c = accept(k->ls, NULL, NULL)) >= 0) {
        nonblock(c);
        assert(k->npending < 4);
        k->pending[k->npending++] = c;
    }
    // A connection is told apart by its request line, which the client sends
    // a step after connecting; until then it waits here unclassified.
    for (int i = 0; i < k->npending; i++) {
        char peek[64] = { 0 };
        recv(k->pending[i], peek, sizeof peek - 1, MSG_PEEK);
        if (!strchr(peek, '\n'))
            continue;
        if (strstr(peek, "/qlcplusWS")) {
            assert(k->ws_fd < 0);
            k->ws_fd = k->pending[i];
            k->have = 0;
        } else {
            assert(k->http_fd < 0);
            k->http_fd = k->pending[i];
        }
        k->pending[i] = k->pending[--k->npending];
        i--;
    }
    if (k->ws_fd >= 0) {
        ssize_t n = read(k->ws_fd, k->buf + k->have, sizeof k->buf - 1 - k->have);
        if (n > 0) {
            k->have += (size_t)n;
            k->buf[k->have] = 0;
        }
        for (;;) {
            char *end = strstr(k->buf, "\r\n\r\n");
            if (end && strstr(k->buf, "Upgrade: websocket")) {
                const char *reply = k->refuse_ws ? "HTTP/1.1 404 Not Found\r\n\r\n"
                    : "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                      "Connection: Upgrade\r\n\r\n";
                assert(write(k->ws_fd, reply, strlen(reply)) > 0);
                size_t used = (size_t)(end + 4 - k->buf);
                memmove(k->buf, k->buf + used, k->have - used);
                k->have -= used;
                k->buf[k->have] = 0;
                continue;
            }
            // A masked client text frame: count it as a heartbeat, drop it.
            if (k->have >= 6 && ((k->buf[0] & 0x0F) == 1 || (k->buf[0] & 0x0F) == 0xA)) {
                size_t len = (unsigned char)k->buf[1] & 0x7F;
                if (k->have < 6 + len)
                    break;
                if ((k->buf[0] & 0x0F) == 1)
                    k->pings_seen++;
                memmove(k->buf, k->buf + 6 + len, k->have - 6 - len);
                k->have -= 6 + len;
                k->buf[k->have] = 0;
                continue;
            }
            break;
        }
    }
    if (k->http_fd >= 0) {
        char req[1024] = { 0 };
        ssize_t n = recv(k->http_fd, req, sizeof req - 1, MSG_PEEK);
        if (n > 0 && strstr(req, "\r\n\r\n") && k->answer_snapshot) {
            assert(read(k->http_fd, req, sizeof req - 1) > 0);
            char head[128];
            int hn = snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n",
                              k->snapshot_len);
            assert(write(k->http_fd, head, (size_t)hn) == hn);
            // The snapshot is large; a non-blocking write may take several
            // rounds, so the socket is left blocking for this one send.
            fcntl(k->http_fd, F_SETFL, fcntl(k->http_fd, F_GETFL) & ~O_NONBLOCK);
            assert(write(k->http_fd, k->snapshot, k->snapshot_len) == (ssize_t)k->snapshot_len);
            close(k->http_fd);
            k->http_fd = -1;
        }
    }
}

static struct qlc_session_config config(int port) {
    struct qlc_session_config c = { .port = port, .heartbeat_ms = 400, .stale_ms = 750,
        .reconnect_ms = 1500, .connect_timeout_ms = 3000, .fetch_timeout_ms = 4000,
        .snapshot_limit = 1024 * 1024 };
    strcpy(c.host, "127.0.0.1");
    return c;
}

// Steps the session with the fake clock advancing `ms` per step and the fake
// serviced between steps. Returns the link state at the end.
static enum qlc_link drive(struct qlc_session *s, struct fake *k, int64_t *now, int steps, int ms) {
    enum qlc_link l = QLC_DOWN;
    for (int i = 0; i < steps; i++) {
        struct pollfd fds[2];
        int n = qlc_session_pollfds(s, fds, 2);
        if (n)
            poll(fds, (nfds_t)n, 2);
        l = qlc_session_step(s, *now);
        if (k)
            fake_service(k);
        *now += ms;
    }
    return l;
}

// Steps until the link reaches `target` or `max_steps` have passed.
static enum qlc_link drive_until(struct qlc_session *s, struct fake *k, int64_t *now,
                                 enum qlc_link target, int max_steps, int ms) {
    enum qlc_link l = QLC_DOWN;
    for (int i = 0; i < max_steps; i++) {
        l = drive(s, k, now, 1, ms);
        if (l == target)
            break;
    }
    return l;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    size_t snap_len;
    char *snap = slurp("tests/dmx-desk/fixtures/vc-vibra.json", &snap_len);
    struct fake k;
    struct qlc_session *s;
    struct qlc_session_config cfg;
    int64_t now;
    char frame[256];

    // Happy path: DOWN -> ... -> READY with a snapshot to take, exactly once.
    fake_init(&k);
    k.snapshot = snap;
    k.snapshot_len = snap_len;
    cfg = config(k.port);
    s = qlc_session_new(&cfg);
    now = 1000;
    if (drive_until(s, &k, &now, QLC_READY, 400, 5) != QLC_READY) { fprintf(stderr, "first: link %d reason [%s]\n", qlc_session_link(s), qlc_session_reason(s)); assert(0); }
    struct vc_doc doc;
    assert(qlc_session_take_snapshot(s, &doc) == 1);
    assert(doc.count > 600);
    assert(qlc_session_take_snapshot(s, &doc) == 0);
    vc_free(&doc);
    // A push from the fake arrives as a frame.
    server_text(k.ws_fd, "FUNCTION|720|Running");
    drive(s, &k, &now, 4, 5);
    assert(qlc_session_recv(s, frame, sizeof frame) == 1 && strcmp(frame, "FUNCTION|720|Running") == 0);
    assert(qlc_session_recv(s, frame, sizeof frame) == 0);
    // Heartbeats keep the link up through a silent master for 2 s: the fake
    // answers each one, and the session never declares it stale.
    int before = k.pings_seen;
    for (int i = 0; i < 400; i++) {
        drive(s, &k, &now, 1, 5);
        if (k.pings_seen > before) {
            server_text(k.ws_fd, "QLC+API|isProjectLoaded|true");
            before = k.pings_seen;
        }
        while (qlc_session_recv(s, frame, sizeof frame) == 1) {}
    }
    assert(qlc_session_link(s) == QLC_READY);
    assert(k.pings_seen >= 4);
    assert(qlc_session_last_rtt(s) >= 0 && qlc_session_last_rtt(s) < 100);
    // A master that stops answering is dropped within stale_ms, with the
    // measured silence in the reason, and a send is refused afterwards.
    assert(qlc_session_send(s, "4|255") == 0);
    enum qlc_link l = drive(s, NULL, &now, 200, 5);     // 1000 ms unanswered
    assert(l == QLC_DOWN);
    assert(strstr(qlc_session_reason(s), "without a word"));
    assert(qlc_session_send(s, "4|255") == -1);
    // It comes back by itself after reconnect_ms, with a fresh snapshot.
    close(k.ws_fd);
    k.ws_fd = -1;
    k.have = 0;
    if (drive_until(s, &k, &now, QLC_READY, 600, 5) != QLC_READY) { fprintf(stderr, "again: link %d reason [%s]\n", qlc_session_link(s), qlc_session_reason(s)); assert(0); }
    assert(qlc_session_take_snapshot(s, &doc) == 1);
    vc_free(&doc);
    qlc_session_free(s);
    fake_close(&k);

    // Replay the live failure: a Ping arrived 230 ms after the last text,
    // then an API response took 514 ms. The old text-only clock dropped at
    // 805 ms, despite having answered the master's Ping only 575 ms before.
    fake_init(&k);
    k.snapshot = snap;
    k.snapshot_len = snap_len;
    cfg = config(k.port);
    s = qlc_session_new(&cfg);
    now = 1000;
    assert(drive_until(s, &k, &now, QLC_READY, 400, 5) == QLC_READY);
    server_text(k.ws_fd, "FUNCTION|720|Running");
    drive(s, &k, &now, 4, 0);
    int64_t base = now;
    const unsigned char ping[] = {0x89, 0x00};
    now = base + 230;
    assert(write(k.ws_fd, ping, sizeof ping) == sizeof ping);
    assert(drive(s, &k, &now, 4, 0) == QLC_READY);
    now = base + 400;
    assert(drive(s, &k, &now, 4, 0) == QLC_READY);
    assert(k.pings_seen == 1);
    now = base + 805;
    assert(drive(s, &k, &now, 4, 0) == QLC_READY);
    assert(k.pings_seen == 1);  // no duplicate probe overwrites the RTT clock
    now = base + 914;
    server_text(k.ws_fd, "QLC+API|isProjectLoaded|true");
    assert(drive(s, &k, &now, 4, 0) == QLC_READY);
    assert(qlc_session_last_rtt(s) == 514);
    assert(qlc_session_step(s, base + 1664) == QLC_READY); // 750 ms
    assert(qlc_session_step(s, base + 1665) == QLC_DOWN);  // 751 ms
    assert(qlc_session_send(s, "4|255") == -1);
    qlc_session_free(s);
    fake_close(&k);

    // Reserve transport time inside the same 750 ms deadline. A 514 ms
    // response without an intervening Ping cannot fit behind a 400 ms idle
    // wait; the desk's earlier probe leaves room, including a 100 ms poll.
    fake_init(&k);
    k.snapshot = snap;
    k.snapshot_len = snap_len;
    cfg = config(k.port);
    cfg.heartbeat_ms = DESK_HEARTBEAT_MS;
    s = qlc_session_new(&cfg);
    now = 1000;
    assert(drive_until(s, &k, &now, QLC_READY, 400, 5) == QLC_READY);
    server_text(k.ws_fd, "FUNCTION|720|Running");
    drive(s, &k, &now, 4, 0);
    base = now;
    now = base + cfg.heartbeat_ms + 100; // worst poll allowance
    assert(drive(s, &k, &now, 4, 0) == QLC_READY);
    assert(k.pings_seen == 1);
    int64_t probe_at = now;
    now += 514;
    assert(drive(s, &k, &now, 4, 0) == QLC_READY); // no reply yet
    server_text(k.ws_fd, "QLC+API|isProjectLoaded|true");
    assert(drive(s, &k, &now, 4, 0) == QLC_READY);
    assert(qlc_session_last_rtt(s) == now - probe_at);
    assert(qlc_session_step(s, now + 750) == QLC_READY);
    assert(qlc_session_step(s, now + 751) == QLC_DOWN);
    qlc_session_free(s);
    fake_close(&k);

    // A dead host: the connect times out, and no step blocks.
    struct qlc_session_config dead = config(9);
    strcpy(dead.host, "10.255.255.1");
    dead.connect_timeout_ms = 300;
    s = qlc_session_new(&dead);
    now = 0;
    for (int i = 0; i < 100; i++) {
        struct pollfd fds[2];
        int n = qlc_session_pollfds(s, fds, 2);
        if (n)
            poll(fds, (nfds_t)n, 2);
        qlc_session_step(s, now);
        now += 10;
    }
    assert(qlc_session_link(s) == QLC_DOWN);
    assert(strstr(qlc_session_reason(s), "timeout") || strstr(qlc_session_reason(s), "refused") ||
           strstr(qlc_session_reason(s), "unreachable") || strstr(qlc_session_reason(s), "down"));
    qlc_session_free(s);

    // A refused upgrade is DOWN with the status line, not READY.
    fake_init(&k);
    k.snapshot = snap;
    k.snapshot_len = snap_len;
    k.refuse_ws = 1;
    cfg = config(k.port);
    s = qlc_session_new(&cfg);
    now = 0;
    assert(drive(s, &k, &now, 200, 5) == QLC_DOWN);
    if (!strstr(qlc_session_reason(s), "404")) { fprintf(stderr, "refusal: reason [%s]\n", qlc_session_reason(s)); assert(0); }
    qlc_session_free(s);
    fake_close(&k);

    // A snapshot that never arrives: FETCHING, then DOWN at the fetch timeout,
    // never READY on nothing.
    fake_init(&k);
    k.snapshot = snap;
    k.snapshot_len = snap_len;
    k.answer_snapshot = 0;
    cfg = config(k.port);
    cfg.fetch_timeout_ms = 500;
    s = qlc_session_new(&cfg);
    now = 0;
    assert(drive(s, &k, &now, 60, 5) == QLC_FETCHING);
    assert(drive(s, &k, &now, 120, 5) == QLC_DOWN);
    assert(strstr(qlc_session_reason(s), "snapshot"));
    qlc_session_free(s);
    fake_close(&k);

    // A snapshot that is not the console: DOWN with "unreadable".
    fake_init(&k);
    k.snapshot = "<html>not the console</html>";
    k.snapshot_len = strlen(k.snapshot);
    cfg = config(k.port);
    s = qlc_session_new(&cfg);
    now = 0;
    assert(drive(s, &k, &now, 200, 5) == QLC_DOWN);
    assert(strstr(qlc_session_reason(s), "unreadable"));
    qlc_session_free(s);
    fake_close(&k);

    free(snap);
    printf("qlc_session ok\n");
    return 0;
}
