#include "wpa_ctrl_dial.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int wpa_ctrl_dial(const char *path, const char *local) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_un mine;
    memset(&mine, 0, sizeof mine);
    mine.sun_family = AF_UNIX;
    snprintf(mine.sun_path, sizeof mine.sun_path, "%s", local);
    unlink(local);
    if (bind(fd, (struct sockaddr *)&mine, sizeof mine) != 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_un daemon;
    memset(&daemon, 0, sizeof daemon);
    daemon.sun_family = AF_UNIX;
    snprintf(daemon.sun_path, sizeof daemon.sun_path, "%s", path);
    if (connect(fd, (struct sockaddr *)&daemon, sizeof daemon) != 0) {
        close(fd);
        unlink(local);
        return -1;
    }
    return fd;
}
