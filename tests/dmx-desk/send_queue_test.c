// SOURCES: send_queue.c
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "send_queue.h"

static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    nonblock(sv[0]);
    struct send_queue q;
    send_queue_init(&q);
    assert(send_queue_empty(&q));

    // A push and a flush deliver the bytes in order.
    assert(send_queue_push(&q, "abc", 3) == 0);
    assert(send_queue_push(&q, "def", 3) == 0);
    assert(send_queue_flush(&q, sv[0]) == 1);
    char got[8] = { 0 };
    assert(read(sv[1], got, 6) == 6 && memcmp(got, "abcdef", 6) == 0);

    // A full socket keeps the rest queued and reports it; nothing is lost.
    unsigned char big[4096];
    memset(big, 'x', sizeof big);
    size_t pushed = 0;
    while (pushed + sizeof big <= SEND_QUEUE_CAP) {
        assert(send_queue_push(&q, big, sizeof big) == 0);
        pushed += sizeof big;
    }
    int rc = send_queue_flush(&q, sv[0]);
    assert(rc == 0 || rc == 1);
    size_t drained = 0;
    unsigned char sink[4096];
    ssize_t n;
    while ((n = read(sv[1], sink, sizeof sink)) > 0) {
        for (ssize_t i = 0; i < n; i++) assert(sink[i] == 'x');
        drained += (size_t)n;
        if (drained == pushed) break;
        assert(send_queue_flush(&q, sv[0]) != -1);
    }
    assert(drained == pushed);
    assert(send_queue_flush(&q, sv[0]) == 1);

    // Past the cap is an error, and the queue is unchanged.
    memset(big, 'y', sizeof big);
    for (size_t i = 0; i < SEND_QUEUE_CAP / sizeof big; i++)
        assert(send_queue_push(&q, big, sizeof big) == 0);
    assert(send_queue_push(&q, "z", 1) == -1);
    assert(q.len == SEND_QUEUE_CAP);

    // A closed peer is an error on flush.
    close(sv[1]);
    assert(send_queue_flush(&q, sv[0]) == -1);
    close(sv[0]);
    printf("send_queue ok\n");
    return 0;
}
