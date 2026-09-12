// SOURCES: master_find.c
// A loopback QLC+ that answers with its title on one port, a listener that
// answers nothing on another: the sweep names the first and not the second,
// and finishes inside its own deadlines.
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "master_find.h"

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int listener(int *port) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(ls, (struct sockaddr *)&a, sizeof a) == 0);
    socklen_t alen = sizeof a;
    assert(getsockname(ls, (struct sockaddr *)&a, &alen) == 0);
    assert(listen(ls, 8) == 0);
    *port = ntohs(a.sin_port);
    return ls;
}

// Serves the fake QLC+ in a child: every connection gets the title.
static pid_t serve(int ls, int answer) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid > 0)
        return pid;
    for (;;) {
        int c = accept(ls, NULL, NULL);
        if (c < 0)
            continue;
        if (answer) {
            char req[256];
            read(c, req, sizeof req);
            const char *reply = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                                "<html><head><title>QLC+ Web Interface</title></head></html>";
            write(c, reply, strlen(reply));
        }
        close(c);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int qlc_port, mute_port;
    int qlc = listener(&qlc_port), mute = listener(&mute_port);
    pid_t a = serve(qlc, 1), b = serve(mute, 0);

    // 127.0.0.1/30 is 127.0.0.1 and 127.0.0.2; the sweep skips itself
    // (given as .2) and probes .1, where the fake listens.
    char out_path[256];
    snprintf(out_path, sizeof out_path, "/tmp/dmxdesk-find-%d", (int)getpid());
    FILE *out = fopen(out_path, "w+");
    assert(out);
    int64_t t0 = now_ms();
    assert(master_find_run("127.0.0.2/30", qlc_port, out) == 1);
    assert(now_ms() - t0 < 3000);
    rewind(out);
    char line[64];
    assert(fgets(line, sizeof line, out) && strcmp(line, "127.0.0.1\n") == 0);
    assert(!fgets(line, sizeof line, out));
    fclose(out);

    // The listener that says nothing is not a master; the whole batch ends
    // at its deadline.
    out = fopen(out_path, "w+");
    t0 = now_ms();
    assert(master_find_run("127.0.0.2/30", mute_port, out) == 0);
    int64_t took = now_ms() - t0;
    assert(took < 1500);
    fclose(out);

    // A port nobody listens on: refused at once.
    out = fopen(out_path, "w+");
    assert(master_find_run("127.0.0.2/30", 1, out) == 0);
    fclose(out);

    // Bounds: a bad cidr, a bad port; a /16 is capped and says so.
    out = fopen(out_path, "w+");
    assert(master_find_run("not an address", 9999, out) == -1);
    assert(master_find_run("127.0.0.2/24", 0, out) == -1);
    fclose(out);
    out = fopen(out_path, "w+");
    t0 = now_ms();
    int n = master_find_run("10.255.0.1/16", 1, out);
    assert(n >= 0 && now_ms() - t0 < 5000);
    rewind(out);
    int partial = 0;
    while (fgets(line, sizeof line, out))
        if (strcmp(line, "partial\n") == 0)
            partial = 1;
    assert(partial);
    fclose(out);

    kill(a, 9);
    kill(b, 9);
    close(qlc);
    close(mute);
    unlink(out_path);
    printf("master_find ok\n");
    return 0;
}
