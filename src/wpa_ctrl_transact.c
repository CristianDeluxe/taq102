#include "wpa_ctrl_transact.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>

int wpa_ctrl_transact(int fd, const char *cmd, char *buf, size_t cap, int timeout_ms) {
    // A reply that came after its request timed out must not answer this one.
    char stale[64];
    while (recv(fd, stale, sizeof stale, MSG_DONTWAIT | MSG_TRUNC) > 0)
        ;
    size_t len = strlen(cmd);
    if (send(fd, cmd, len, 0) != (ssize_t)len)
        return -1;
    struct pollfd p = { .fd = fd, .events = POLLIN, .revents = 0 };
    if (poll(&p, 1, timeout_ms) <= 0 || !(p.revents & POLLIN)) {
        errno = ETIMEDOUT;
        return -1;
    }
    // One datagram is one reply; a reply that would not fit is refused whole
    // rather than handed back cut, so a caller never parses half a table.
    ssize_t n = recv(fd, buf, cap, MSG_TRUNC);
    if (n < 0)
        return -1;
    if ((size_t)n >= cap) {
        errno = EMSGSIZE;
        return -1;
    }
    buf[n] = '\0';
    return (int)n;
}
