// One blocking HTTP GET, for fetching /vc.json from the master. Not a client
// library: no redirects, no chunked encoding, no keep-alive, no TLS. The
// server is QLC+ on the show's own network and it sends a plain body with a
// Content-Length.
#ifndef HTTP_GET_H
#define HTTP_GET_H

#include <stddef.h>

// On success returns 0 and hands back a NUL-terminated body the caller frees.
// `limit` caps the body; a larger Content-Length fails rather than allocating
// it. -1 on any failure, with the reason on stderr.
int http_get(const char *host, int port, const char *path, int timeout_ms,
             size_t limit, char **body, size_t *len);

#endif
