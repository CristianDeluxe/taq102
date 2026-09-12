// A WebSocket client with exactly what QLC+'s web access needs: one TCP
// connection, the opening handshake, masked text frames out, text frames in.
// No TLS, no extensions, no fragmentation on send. Server frames may be
// fragmented and are reassembled up to a bounded size; anything larger closes
// the connection rather than growing a buffer for a stranger.
#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stddef.h>

struct ws;

// Blocking connect and handshake, with a timeout in milliseconds for each.
// NULL on any failure, with the reason on stderr.
struct ws *ws_connect(const char *host, int port, const char *path,
                      int timeout_ms);

// The socket, for poll(). Reading it directly is not supported.
int ws_fd(const struct ws *ws);

// One text frame. 0 on success, -1 when the connection is gone or the text is
// longer than a command may be.
int ws_send_text(struct ws *ws, const char *text);

// Non-blocking. 1 and a NUL-terminated frame in `buf`, 0 when nothing has
// arrived yet, -1 when the connection has closed or broken. Call it until it
// returns 0: one read can carry several frames.
int ws_recv_text(struct ws *ws, char *buf, size_t cap);

void ws_close(struct ws *ws);

#endif
