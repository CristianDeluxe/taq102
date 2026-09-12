#include "wpa_ctrl.h"
#include <stdio.h>
#include <unistd.h>
#include "wpa_ctrl_dial.h"
#include "wpa_ctrl_private.h"

void wpa_ctrl_abandon(struct wpa_ctrl *c) {
    if (!c)
        return;
    if (c->fd >= 0)
        close(c->fd);
    unlink(c->local);
    c->pending = 0;
    c->generation++;
    int n = snprintf(c->local, sizeof c->local, "%s/dmxdesk-ctrl-%d-%d",
                     c->directory, (int)getpid(), c->generation);
    if (n <= 0 || (size_t)n >= sizeof c->local) {
        c->local[0] = '\0';
        c->fd = -1;
        return;
    }
    c->fd = wpa_ctrl_dial(c->path, c->local);
}
