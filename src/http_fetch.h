// One non-blocking HTTP GET, for the console snapshot. Not a client library:
// no redirects, no chunked encoding, no keep-alive, no TLS. The server is
// QLC+ on the show's own network and it sends a plain body with a
// Content-Length. The caller polls the fd for the events asked for and steps
// the fetch; nothing here blocks, so a slow master cannot freeze touch.
#ifndef HTTP_FETCH_H
#define HTTP_FETCH_H

#include <stddef.h>

enum http_fetch_state { HTTP_FETCH_CONNECTING, HTTP_FETCH_READING,
                        HTTP_FETCH_DONE, HTTP_FETCH_FAILED };

struct http_fetch;

// Resolves and starts a non-blocking connect. NULL only when the address
// cannot be resolved or no socket can be made. `limit` bounds the body: a
// larger Content-Length fails before a byte of it is stored.
struct http_fetch *http_fetch_start(const char *host, int port, const char *path,
                                    size_t limit);
int http_fetch_fd(const struct http_fetch *f);
short http_fetch_poll_events(const struct http_fetch *f);
enum http_fetch_state http_fetch_step(struct http_fetch *f);
const char *http_fetch_reason(const struct http_fetch *f);
// Valid after HTTP_FETCH_DONE: a NUL-terminated body owned by the fetch until
// http_fetch_free; `len` excludes the NUL.
const char *http_fetch_body(const struct http_fetch *f, size_t *len);
void http_fetch_free(struct http_fetch *f);

#endif
