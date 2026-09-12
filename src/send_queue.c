#include "send_queue.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>

// macOS has no MSG_NOSIGNAL; the host tests ignore SIGPIPE themselves.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

void send_queue_init(struct send_queue *q) {
    q->head = 0;
    q->len = 0;
}

int send_queue_empty(const struct send_queue *q) { return q->len == 0; }

// A flat buffer that slides back to the start when a push finds room only at
// the front: simpler than a ring, and a queue that ever holds more than a few
// frames is already a link that has failed.
int send_queue_push(struct send_queue *q, const void *data, size_t n) {
    if (n > SEND_QUEUE_CAP - q->len)
        return -1;
    if (q->head + q->len + n > SEND_QUEUE_CAP) {
        memmove(q->buf, q->buf + q->head, q->len);
        q->head = 0;
    }
    memcpy(q->buf + q->head + q->len, data, n);
    q->len += n;
    return 0;
}

int send_queue_flush(struct send_queue *q, int fd) {
    while (q->len > 0) {
        ssize_t n = send(fd, q->buf + q->head, q->len, MSG_NOSIGNAL);
        if (n > 0) {
            q->head += (size_t)n;
            q->len -= (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        return -1;
    }
    q->head = 0;
    return 1;
}
