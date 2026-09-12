#include "wpa_ctrl.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "wpa_ctrl_private.h"

int wpa_ctrl_begin(struct wpa_ctrl *c, const char *cmd) {
    if (!c || c->fd < 0 || !cmd || !cmd[0])
        return -1;
    if (c->pending) {
        errno = EBUSY;
        return -1;
    }
    size_t len = strlen(cmd);
    if (send(c->fd, cmd, len, 0) != (ssize_t)len)
        return -1;
    c->pending = 1;
    return 0;
}
