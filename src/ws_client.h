// A WebSocket client with exactly what QLC+'s web access needs: one TCP
// connection, the opening handshake, masked text frames out, text frames in.
// No TLS, no extensions, no fragmentation on send. Server frames may be
// fragmented and are reassembled up to a bounded size; anything larger closes
// the connection rather than growing a buffer for a stranger.
//
// Nothing here blocks. Connecting and shaking hands are a state machine the
// caller advances with ws_step whenever poll reports the events
// ws_poll_events asked for; sends go through a bounded queue that ws_flush
// drains when the socket can take them.
#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stddef.h>

struct ws;

enum ws_state { WS_CONNECTING, WS_HANDSHAKING, WS_OPEN, WS_FAILED };

// Resolves the host and starts a non-blocking connect. NULL only when the
// address cannot be resolved or no socket can be made.
struct ws *ws_start(const char *host, int port, const char *path);

// Drives the connect and the handshake. Returns the state after the step;
// WS_FAILED carries its reason in ws_reason(). Harmless to call when open.
enum ws_state ws_step(struct ws *ws);
enum ws_state ws_state(const struct ws *ws);
const char *ws_reason(const struct ws *ws);

// What poll() should wait for right now: POLLOUT while connecting or while
// bytes are queued, POLLIN once there is something to read for.
short ws_poll_events(const struct ws *ws);

// The socket, for poll(). Reading it directly is not supported.
int ws_fd(const struct ws *ws);

// Queues one masked text frame. -1 when the link is not open, the text is
// longer than a command may be, or the queue is full: the caller closes.
int ws_send_text(struct ws *ws, const char *text);

// Writes queued bytes. 1 when the queue is empty afterwards, 0 when bytes
// remain, -1 when the socket is gone.
int ws_flush(struct ws *ws);

// Non-blocking. 1 and a NUL-terminated frame in `buf`, 0 when nothing has
// arrived yet, -1 when the connection has closed or broken. Call it until it
// returns 0: one read can carry several frames.
int ws_recv_text(struct ws *ws, char *buf, size_t cap);

// Wraps an already-connected socket as WS_OPEN with no handshake. For tests
// that speak frames over a socketpair.
struct ws *ws_adopt(int fd);

void ws_close(struct ws *ws);

#endif
