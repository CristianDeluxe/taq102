#include "wpa_ctrl.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include "wpa_ctrl_private.h"

int wpa_ctrl_reply(struct wpa_ctrl *c, char *buf, size_t cap) {
    if (!c || c->fd < 0 || !buf || cap < 2)
        return -1;
    if (!c->pending)
        return 0;
    struct iovec iov = { .iov_base = buf, .iov_len = cap - 1 };
    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    ssize_t n = recvmsg(c->fd, &msg, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        c->pending = 0;
        return -1;
    }
    c->pending = 0;
    if (msg.msg_flags & MSG_TRUNC) {
        errno = EMSGSIZE;
        return -1;
    }
    buf[n] = '\0';
    return 1;
}
