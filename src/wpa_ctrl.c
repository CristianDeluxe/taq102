#include "wpa_ctrl.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wpa_ctrl_dial.h"
#include "wpa_ctrl_private.h"
#include "wpa_ctrl_transact.h"

struct wpa_ctrl *wpa_ctrl_open(const char *path) {
    if (!path)
        return NULL;
    struct wpa_ctrl *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    snprintf(c->path, sizeof c->path, "%s", path);
    const char *directory = getenv("TMPDIR");
    if (!directory || !directory[0])
        directory = "/tmp";
    snprintf(c->directory, sizeof c->directory, "%s", directory);
    int n = snprintf(c->local, sizeof c->local, "%s/dmxdesk-ctrl-%d", directory, (int)getpid());
    int e = snprintf(c->local_event, sizeof c->local_event, "%s/dmxdesk-ctrl-%d-ev", directory, (int)getpid());
    if (n < 0 || (size_t)n >= sizeof c->local || e < 0 || (size_t)e >= sizeof c->local_event) {
        free(c);
        return NULL;
    }
    c->fd = wpa_ctrl_dial(path, c->local);
    c->event_fd = c->fd >= 0 ? wpa_ctrl_dial(path, c->local_event) : -1;
    if (c->fd < 0 || c->event_fd < 0) {
        fprintf(stderr, "wpa_ctrl: cannot reach %s: %s\n", path, strerror(errno));
        wpa_ctrl_close(c);
        return NULL;
    }
    char reply[16];
    if (wpa_ctrl_transact(c->event_fd, "ATTACH", reply, sizeof reply, 1000) < 0 ||
        strncmp(reply, "OK", 2) != 0) {
        fprintf(stderr, "wpa_ctrl: the daemon at %s refused ATTACH\n", path);
        wpa_ctrl_close(c);
        return NULL;
    }
    return c;
}
