// SOURCES: ws_client.c send_queue.c
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include "ws_client.h"

int main(void) {
    int fd[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    assert(fcntl(fd[0], F_SETFL, O_NONBLOCK) == 0);
    struct ws *ws = ws_adopt(fd[0]);
    char text[32];
    int activity = 1;
    assert(ws_recv_text(ws, text, sizeof text, &activity) == 0 && activity == 0);
    const unsigned char ping[] = {0x89, 0x00};
    assert(write(fd[1], ping, 1) == 1);
    assert(ws_recv_text(ws, text, sizeof text, &activity) == 0 && activity == 0);
    assert(write(fd[1], ping + 1, 1) == 1);
    assert(ws_recv_text(ws, text, sizeof text, &activity) == 0 && activity == 1);
    assert(ws_flush(ws) == 1);
    unsigned char pong[6];
    assert(read(fd[1], pong, sizeof pong) == sizeof pong && pong[0] == 0x8a);
    assert(ws_recv_text(ws, text, sizeof text, &activity) == 0 && activity == 0);
    const unsigned char incoming_pong[] = {0x8a, 0x00};
    assert(write(fd[1], incoming_pong, sizeof incoming_pong) == sizeof incoming_pong);
    assert(ws_recv_text(ws, text, sizeof text, &activity) == 0 && activity == 1);
    const unsigned char fragment[] = {0x01, 0x01, 'a'};
    assert(write(fd[1], fragment, sizeof fragment) == sizeof fragment);
    assert(ws_recv_text(ws, text, sizeof text, &activity) == 0 && activity == 0);
    const unsigned char invalid_ping[] = {0x09, 0x00};
    assert(write(fd[1], invalid_ping, sizeof invalid_ping) == sizeof invalid_ping);
    assert(ws_recv_text(ws, text, sizeof text, &activity) == -1 && activity == 0);
    ws_close(ws);
    close(fd[1]);
    puts("ws_activity ok");
    return 0;
}
