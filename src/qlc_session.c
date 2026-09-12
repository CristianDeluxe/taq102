#include "qlc_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_fetch.h"
#include "ws_client.h"

// Frames that arrive while the snapshot is still being fetched, and frames
// the caller has not drained yet. A push is short; 64 of them is far more
// than a console produces between two loop turns.
#define RING_FRAMES 64
#define RING_FRAME_MAX 256

struct qlc_session {
    struct qlc_session_config cfg;
    enum qlc_link link;
    struct ws *ws;
    struct http_fetch *fetch;
    int64_t next_try_ms, phase_started_ms, last_heard_ms, last_beat_ms;
    int64_t beat_sent_ms;       // 0 when no heartbeat is outstanding
    int last_rtt_ms;
    char ring[RING_FRAMES][RING_FRAME_MAX];
    int ring_head, ring_count;
    struct vc_doc snapshot;
    int snapshot_pending;
    char reason[96];
};

struct qlc_session *qlc_session_new(const struct qlc_session_config *cfg) {
    if (!cfg)
        return NULL;
    struct qlc_session *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->cfg = *cfg;
    s->link = QLC_DOWN;
    snprintf(s->reason, sizeof s->reason, "not connected yet");
    return s;
}

static void drop(struct qlc_session *s, int64_t now, const char *reason) {
    if (s->ws) {
        ws_close(s->ws);
        s->ws = NULL;
    }
    if (s->fetch) {
        http_fetch_free(s->fetch);
        s->fetch = NULL;
    }
    if (s->snapshot_pending) {
        vc_free(&s->snapshot);
        s->snapshot_pending = 0;
    }
    s->ring_head = 0;
    s->ring_count = 0;
    s->link = QLC_DOWN;
    s->next_try_ms = now + s->cfg.reconnect_ms;
    snprintf(s->reason, sizeof s->reason, "%s", reason);
}

void qlc_session_free(struct qlc_session *s) {
    if (!s)
        return;
    drop(s, 0, "closed");
    free(s);
}

void qlc_session_set_host(struct qlc_session *s, const char *host, int port) {
    if (!s || !host)
        return;
    snprintf(s->cfg.host, sizeof s->cfg.host, "%s", host);
    s->cfg.port = port;
    drop(s, 0, "master changed");
    s->next_try_ms = 0;
}

void qlc_session_refresh(struct qlc_session *s, int64_t now_ms) {
    if (!s || s->link != QLC_READY || s->fetch)
        return;
    s->fetch = http_fetch_start(s->cfg.host, s->cfg.port, "/vc.json", s->cfg.snapshot_limit);
    if (!s->fetch)
        return;
    s->link = QLC_FETCHING;
    s->phase_started_ms = now_ms;
    snprintf(s->reason, sizeof s->reason, "re-reading the show");
}

int qlc_session_pollfds(const struct qlc_session *s, struct pollfd *fds, int cap) {
    int n = 0;
    if (!s || !fds)
        return 0;
    if (s->ws && n < cap) {
        fds[n].fd = ws_fd(s->ws);
        fds[n].events = ws_poll_events(s->ws);
        fds[n].revents = 0;
        n++;
    }
    if (s->fetch && n < cap) {
        fds[n].fd = http_fetch_fd(s->fetch);
        fds[n].events = http_fetch_poll_events(s->fetch);
        fds[n].revents = 0;
        n++;
    }
    return n;
}

// Drains the socket into the ring so its buffer never fills while the
// snapshot is being read. -1 when the socket is gone.
static int pump_frames(struct qlc_session *s, int64_t now) {
    char frame[RING_FRAME_MAX];
    int r;
    while ((r = ws_recv_text(s->ws, frame, sizeof frame)) == 1) {
        s->last_heard_ms = now;
        if (s->beat_sent_ms && strncmp(frame, "QLC+API|isProjectLoaded", 23) == 0) {
            s->last_rtt_ms = (int)(now - s->beat_sent_ms);
            s->beat_sent_ms = 0;
        }
        if (s->ring_count == RING_FRAMES) {
            // The oldest frame goes: a desk that cannot keep up with pushes
            // resynchronises from the next snapshot anyway.
            s->ring_head = (s->ring_head + 1) % RING_FRAMES;
            s->ring_count--;
        }
        int slot = (s->ring_head + s->ring_count) % RING_FRAMES;
        memcpy(s->ring[slot], frame, sizeof frame);
        s->ring_count++;
    }
    return r;
}

enum qlc_link qlc_session_step(struct qlc_session *s, int64_t now) {
    if (!s)
        return QLC_DOWN;
    switch (s->link) {
    case QLC_DOWN:
        if (now < s->next_try_ms)
            break;
        // No master yet: nothing to dial. The reason names it for the card.
        if (!s->cfg.host[0]) {
            snprintf(s->reason, sizeof s->reason, "no master set");
            s->next_try_ms = now + s->cfg.reconnect_ms;
            break;
        }
        s->ws = ws_start(s->cfg.host, s->cfg.port, "/qlcplusWS");
        if (!s->ws) {
            drop(s, now, "cannot resolve the master");
            break;
        }
        s->link = QLC_CONNECTING;
        s->phase_started_ms = now;
        break;

    case QLC_CONNECTING: {
        enum ws_state st = ws_step(s->ws);
        if (st == WS_FAILED) {
            char reason[96];
            snprintf(reason, sizeof reason, "%s", ws_reason(s->ws));
            drop(s, now, reason);
            break;
        }
        if (st != WS_OPEN) {
            if (now - s->phase_started_ms > s->cfg.connect_timeout_ms)
                drop(s, now, "connect timeout");
            break;
        }
        // Every connection re-reads the console: the socket sends no
        // snapshot, and the document is the only source that says which
        // widget a state belongs to. It also catches a master that loaded a
        // different workspace while the tablet was away.
        s->fetch = http_fetch_start(s->cfg.host, s->cfg.port, "/vc.json",
                                    s->cfg.snapshot_limit);
        if (!s->fetch) {
            drop(s, now, "snapshot: cannot start the fetch");
            break;
        }
        s->link = QLC_FETCHING;
        s->phase_started_ms = now;
        break;
    }

    case QLC_FETCHING: {
        if (ws_step(s->ws) == WS_FAILED || pump_frames(s, now) < 0) {
            drop(s, now, "socket closed while reading the show");
            break;
        }
        enum http_fetch_state st = http_fetch_step(s->fetch);
        if (st == HTTP_FETCH_FAILED) {
            char reason[96];
            snprintf(reason, sizeof reason, "snapshot: %s", http_fetch_reason(s->fetch));
            drop(s, now, reason);
            break;
        }
        if (st != HTTP_FETCH_DONE) {
            if (now - s->phase_started_ms > s->cfg.fetch_timeout_ms)
                drop(s, now, "snapshot timeout");
            break;
        }
        size_t len;
        const char *body = http_fetch_body(s->fetch, &len);
        if (s->snapshot_pending)
            vc_free(&s->snapshot);
        s->snapshot_pending = 0;
        if (vc_parse(body, len, &s->snapshot) != 0) {
            drop(s, now, "snapshot unreadable: not a console this desk can read");
            break;
        }
        http_fetch_free(s->fetch);
        s->fetch = NULL;
        s->snapshot_pending = 1;
        // The clock on silence starts once the desk is actually listening:
        // counting the fetch as the master saying nothing is what made the
        // first connections drop themselves.
        s->last_heard_ms = now;
        s->last_beat_ms = now;
        s->beat_sent_ms = 0;
        s->last_rtt_ms = -1;
        s->link = QLC_READY;
        snprintf(s->reason, sizeof s->reason, "linked");
        break;
    }

    case QLC_READY: {
        if (ws_step(s->ws) == WS_FAILED || pump_frames(s, now) < 0) {
            drop(s, now, "socket closed");
            break;
        }
        // The master pushes only when something changes and its own ping is
        // every five seconds, so the desk asks a question of its own well
        // inside the window it treats as stale.
        if (now - s->last_beat_ms >= s->cfg.heartbeat_ms) {
            s->last_beat_ms = now;
            s->beat_sent_ms = now;
            if (ws_send_text(s->ws, "QLC+API|isProjectLoaded") != 0) {
                drop(s, now, "socket refused the heartbeat");
                break;
            }
        }
        if (ws_flush(s->ws) < 0) {
            drop(s, now, "socket closed");
            break;
        }
        if (now - s->last_heard_ms > s->cfg.stale_ms) {
            char reason[96];
            snprintf(reason, sizeof reason, "%lld ms without a word from the master",
                     (long long)(now - s->last_heard_ms));
            drop(s, now, reason);
        }
        break;
    }
    }
    return s->link;
}

int qlc_session_take_snapshot(struct qlc_session *s, struct vc_doc *out) {
    if (!s || !out || !s->snapshot_pending)
        return 0;
    *out = s->snapshot;
    memset(&s->snapshot, 0, sizeof s->snapshot);
    s->snapshot_pending = 0;
    return 1;
}

int qlc_session_recv(struct qlc_session *s, char *buf, size_t cap) {
    if (!s || !buf || cap == 0 || s->ring_count == 0)
        return 0;
    snprintf(buf, cap, "%s", s->ring[s->ring_head]);
    s->ring_head = (s->ring_head + 1) % RING_FRAMES;
    s->ring_count--;
    return 1;
}

int qlc_session_send(struct qlc_session *s, const char *frame) {
    if (!s || !frame || s->link != QLC_READY)
        return -1;
    if (ws_send_text(s->ws, frame) != 0) {
        drop(s, s->last_beat_ms, "socket refused a command");
        return -1;
    }
    return 0;
}

enum qlc_link qlc_session_link(const struct qlc_session *s) { return s ? s->link : QLC_DOWN; }
int qlc_session_last_rtt(const struct qlc_session *s) { return s ? s->last_rtt_ms : -1; }
const char *qlc_session_reason(const struct qlc_session *s) { return s ? s->reason : "no session"; }
