// The desk's connection to the master as a sequence, not an event: opening
// the socket sends no snapshot, so READY means the socket is open AND a fresh
// console document has been fetched and parsed on this very connection.
// Every failure carries a reason and a retry deadline. Nothing here blocks;
// the caller polls the fds this hands out and steps the session.
#ifndef QLC_SESSION_H
#define QLC_SESSION_H

#include <poll.h>
#include <stddef.h>
#include <stdint.h>

#include "vcjson.h"

enum qlc_link { QLC_DOWN, QLC_CONNECTING, QLC_FETCHING, QLC_READY };

struct qlc_session_config {
    char host[128];
    int port;
    int heartbeat_ms;         // the desk's own question, well inside stale_ms
    int stale_ms;             // silence after which the master is gone
    int reconnect_ms;         // wait between attempts
    int connect_timeout_ms;   // socket connect plus handshake
    int fetch_timeout_ms;     // the snapshot
    size_t snapshot_limit;    // bytes of /vc.json this desk will read
};

struct qlc_session;

struct qlc_session *qlc_session_new(const struct qlc_session_config *cfg);
void qlc_session_free(struct qlc_session *s);

// Changes the master. Drops any link at once; the next step reconnects.
void qlc_session_set_host(struct qlc_session *s, const char *host, int port);

// Fills up to two pollfds and returns how many. The caller polls them with
// its own timeout and then calls qlc_session_step.
int qlc_session_pollfds(const struct qlc_session *s, struct pollfd *fds, int cap);

// Everything time-driven: retry deadlines, the connect and fetch timeouts,
// the heartbeat, staleness. Returns the link state after the step.
enum qlc_link qlc_session_step(struct qlc_session *s, int64_t now_ms);

// 1 when this connection produced a fresh validated snapshot the caller has
// not taken yet: the caller owns `out` afterwards and rebuilds its routes.
int qlc_session_take_snapshot(struct qlc_session *s, struct vc_doc *out);

// Frames received since the last call, one at a time. 1 and a frame, 0 none.
int qlc_session_recv(struct qlc_session *s, char *buf, size_t cap);

// Queues a frame. -1 when not READY: the frame is dropped, never kept for
// later, because a toggle sent late is a second toggle.
int qlc_session_send(struct qlc_session *s, const char *frame);

enum qlc_link qlc_session_link(const struct qlc_session *s);
// Why the link is down, or why it was last lost.
const char *qlc_session_reason(const struct qlc_session *s);

#endif
