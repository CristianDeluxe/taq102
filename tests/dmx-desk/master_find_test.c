// SOURCES: master_find_ports.c master_find_peer_error.c
// Real ephemeral loopback HTTP peers, with syscall wrappers for hermetic
// known-port mapping, exact attempt accounting and local failure injection.
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "master_find.h"
#include "master_find_ports.h"

static int probe_socket(int domain, int type, int protocol);
static int probe_connect(int fd, const struct sockaddr *address, socklen_t len);
static int probe_close(int fd);
static int probe_poll(struct pollfd *fds, nfds_t count, int timeout);
static int probe_fcntl(int fd, int command, int argument);
#define socket probe_socket
#define connect probe_connect
#define close probe_close
#define poll probe_poll
#define fcntl probe_fcntl
#include "../../src/master_find.c"
#undef socket
#undef connect
#undef close
#undef poll
#undef fcntl

static int active, peak, attempts, configured, mapped_9998, mapped_9999;
static int socket_failure, connect_failure, poll_failure, fcntl_failure, stall;
static unsigned char visited[257][MASTER_FIND_MAX_PORTS];

static int probe_socket(int domain, int type, int protocol) {
    if (socket_failure && active == socket_failure - 1) {
        errno = EMFILE;
        return -1;
    }
    int fd = socket(domain, type, protocol);
    if (fd >= 0 && ++active > peak)
        peak = active;
    assert(active <= MASTER_FIND_BATCH);
    return fd;
}

static int probe_close(int fd) {
    assert(active > 0);
    active--;
    return close(fd);
}

static int probe_fcntl(int fd, int command, int argument) {
    if (fcntl_failure) {
        errno = EBADF;
        return -1;
    }
    return fcntl(fd, command, argument);
}

static int probe_poll(struct pollfd *fds, nfds_t count, int timeout) {
    if (poll_failure) {
        errno = EIO;
        return -1;
    }
    if (stall) {
        if (timeout > 0)
            usleep((useconds_t)timeout * 1000);
        return 0;
    }
    return poll(fds, count, timeout);
}

static int probe_connect(int fd, const struct sockaddr *address, socklen_t len) {
    struct sockaddr_in target = *(const struct sockaddr_in *)address;
    unsigned host = ntohl(target.sin_addr.s_addr);
    assert((host >> 24) == 127); // Never let a test reach the real network.
    unsigned index = host & 0xffff;
    assert(index > 0 && index <= MASTER_FIND_MAX_HOSTS);
    int port = ntohs(target.sin_port);
    int ports[MASTER_FIND_MAX_PORTS];
    int count = master_find_ports(configured, ports), slot = -1;
    for (int i = 0; i < count; i++)
        if (ports[i] == port)
            slot = i;
    assert(slot >= 0 && !visited[index][slot]);
    visited[index][slot] = 1;
    attempts++;
    if (connect_failure) {
        errno = connect_failure;
        return -1;
    }
    if (stall) {
        errno = EINPROGRESS;
        return -1;
    }
    if (port == 9998)
        port = mapped_9998;
    else if (port == 9999)
        port = mapped_9999;
    if (!port || index != 1) {
        errno = ECONNREFUSED;
        return -1;
    }
    target.sin_port = htons((uint16_t)port);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return connect(fd, (const struct sockaddr *)&target, len);
}

static int listener(int *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    assert(bind(fd, (struct sockaddr *)&a, sizeof a) == 0);
    socklen_t len = sizeof a;
    assert(getsockname(fd, (struct sockaddr *)&a, &len) == 0);
    assert(listen(fd, 8) == 0);
    *port = ntohs(a.sin_port);
    assert(*port != 9998 && *port != 9999);
    return fd;
}

static pid_t serve(int listener, int answer) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid)
        return pid;
    for (;;) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0)
            continue;
        char request[256];
        if (read(fd, request, sizeof request) > 0 && answer) {
            const char reply[] = "HTTP/1.0 200 OK\r\n\r\n<title>QLC+ Web Interface</title>";
            (void)write(fd, reply, sizeof reply - 1);
        }
        close(fd);
    }
}

static int sweep(const char *cidr, int port, FILE *out) {
    assert(active == 0);
    peak = attempts = 0;
    memset(visited, 0, sizeof visited);
    configured = port;
    rewind(out);
    assert(ftruncate(fileno(out), 0) == 0);
    int result = master_find_run(cidr, port, out);
    assert(active == 0);
    rewind(out);
    return result;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int ports[MASTER_FIND_MAX_PORTS];
    assert(master_find_ports(9998, ports) == 2 && ports[0] == 9998 && ports[1] == 9999);
    assert(master_find_ports(9999, ports) == 2 && ports[0] == 9999 && ports[1] == 9998);
    assert(master_find_ports(12345, ports) == 3 && ports[0] == 12345 && ports[1] == 9998 && ports[2] == 9999);
    assert(master_find_ports(0, ports) == -1 && master_find_ports(65536, ports) == -1);

    int qlc_port, mute_port;
    int qlc = listener(&qlc_port), mute = listener(&mute_port);
    pid_t a = serve(qlc, 1), b = serve(mute, 0);
    FILE *out = tmpfile();
    assert(out);
    char line[64], expected[64];

    // An arbitrary configured port is always included and reported verbatim.
    assert(sweep("127.0.0.2/30", qlc_port, out) == 1);
    snprintf(expected, sizeof expected, "127.0.0.1:%d\n", qlc_port);
    assert(fgets(line, sizeof line, out) && !strcmp(line, expected));
    assert(!fgets(line, sizeof line, out) && attempts == 3);

    // A master on 9998 is found even with a different configured port. Both
    // known logical ports map to ephemeral listeners; no fixed ports are bound.
    mapped_9998 = qlc_port;
    mapped_9999 = qlc_port;
    assert(sweep("127.0.0.2/30", mute_port, out) == 2);
    assert(fgets(line, sizeof line, out) && !strcmp(line, "127.0.0.1:9998\n"));
    assert(fgets(line, sizeof line, out) && !strcmp(line, "127.0.0.1:9999\n"));
    assert(!fgets(line, sizeof line, out) && attempts == 3);
    assert(sweep("127.0.0.2/30", 9999, out) == 2 && attempts == 2);
    assert(fgets(line, sizeof line, out) && !strcmp(line, "127.0.0.1:9999\n"));
    mapped_9998 = mapped_9999 = 0;
    assert(sweep("127.0.0.2/30", mute_port, out) == 0);
    assert(!fgets(line, sizeof line, out));

    assert(sweep("not an address", 9999, out) == -1 && attempts == 0);
    assert(sweep("127.0.0.2/24", 0, out) == -1 && attempts == 0);

    // Silent connects consume all twelve deadlines. Self is outside the cap:
    // exactly 256 * 3 attempts, no duplicates, 64 descriptors at peak.
    stall = 1;
    int64_t began = now_ms();
    assert(sweep("127.0.2.1/16", mute_port, out) == 0);
    int64_t elapsed = now_ms() - began;
    assert(attempts == MASTER_FIND_MAX_HOSTS * MASTER_FIND_MAX_PORTS);
    assert(peak == MASTER_FIND_BATCH);
    assert(elapsed >= 4800 && elapsed < 6500);
    assert(fgets(line, sizeof line, out) && !strcmp(line, "partial\n"));
    assert(!fgets(line, sizeof line, out));
    for (int h = 1; h <= MASTER_FIND_MAX_HOSTS; h++)
        for (int p = 0; p < MASTER_FIND_MAX_PORTS; p++)
            assert(visited[h][p]);
    stall = 0;

    // Self inside the first batch used to duplicate the next boundary host.
    assert(sweep("127.0.0.2/24", mute_port, out) == 0 && attempts == 253 * 3);
    for (int h = 1; h <= 254; h++)
        for (int p = 0; p < 3; p++)
            assert(visited[h][p] == (h != 2));
    assert(!fgets(line, sizeof line, out));

    socket_failure = 6;
    stall = 1;
    assert(sweep("127.0.0.2/24", mute_port, out) == -1 && errno == EMFILE);
    socket_failure = stall = 0;
    connect_failure = EPERM;
    assert(sweep("127.0.0.2/30", mute_port, out) == -1 && errno == EPERM);
    connect_failure = ENETUNREACH;
    assert(sweep("127.0.0.2/30", mute_port, out) == -1 && errno == ENETUNREACH);
    connect_failure = 0;
    poll_failure = 1;
    assert(sweep("127.0.0.2/30", mute_port, out) == -1 && errno == EIO);
    poll_failure = 0;
    fcntl_failure = 1;
    assert(sweep("127.0.0.2/30", mute_port, out) == -1 && errno == EBADF);
    fcntl_failure = 0;

    // These are the two exact child PIDs created above, never process patterns.
    assert(kill(a, SIGTERM) == 0 && kill(b, SIGTERM) == 0);
    assert(waitpid(a, NULL, 0) == a && waitpid(b, NULL, 0) == b);
    close(qlc);
    close(mute);
    fclose(out);
    printf("master_find ok (768 attempts, peak 64 sockets, silent sweep %lld ms)\n", (long long)elapsed);
    return 0;
}
