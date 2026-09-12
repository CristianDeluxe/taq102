#include "wpa_ctrl.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include "wpa_ctrl_private.h"

int wpa_ctrl_request(struct wpa_ctrl *c, const char *cmd, char *buf, size_t cap,
                     int timeout_ms) {
    if (!buf || cap < 2 || timeout_ms < 0 || wpa_ctrl_begin(c, cmd) != 0)
        return -1;
    struct pollfd p = { .fd = c->fd, .events = POLLIN, .revents = 0 };
    if (poll(&p, 1, timeout_ms) > 0 && (p.revents & POLLIN)) {
        int rc = wpa_ctrl_reply(c, buf, cap);
        if (rc == 1)
            return (int)strlen(buf);
        if (rc < 0 && errno == EMSGSIZE)
            return -1;
    }
    wpa_ctrl_abandon(c);
    errno = ETIMEDOUT;
    return -1;
}
