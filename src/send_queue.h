// A bounded queue of bytes waiting for a non-blocking socket. The desk's loop
// never blocks on a send: a frame is appended here and flushed when poll says
// the socket can take it. A queue that fills is a link that has failed, and
// the caller closes it rather than waiting.
#ifndef SEND_QUEUE_H
#define SEND_QUEUE_H

#include <stddef.h>

#define SEND_QUEUE_CAP (16 * 1024)

struct send_queue {
    unsigned char buf[SEND_QUEUE_CAP];
    size_t head, len;
};

void send_queue_init(struct send_queue *q);
// 0 when appended, -1 when it would not fit; the queue is unchanged then.
int send_queue_push(struct send_queue *q, const void *data, size_t n);
// Writes what the socket takes now. 1 when empty afterwards, 0 when bytes
// remain (poll for POLLOUT), -1 on a socket error.
int send_queue_flush(struct send_queue *q, int fd);
int send_queue_empty(const struct send_queue *q);

#endif
