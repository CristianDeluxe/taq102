#include "wpa_ctrl.h"
#include <stdlib.h>
#include <unistd.h>
#include "wpa_ctrl_private.h"
#include "wpa_ctrl_transact.h"

void wpa_ctrl_close(struct wpa_ctrl *c) {
    if (!c)
        return;
    if (c->event_fd >= 0) {
        char reply[16];
        wpa_ctrl_transact(c->event_fd, "DETACH", reply, sizeof reply, 200);
        close(c->event_fd);
    }
    if (c->fd >= 0)
        close(c->fd);
    if (c->local[0])
        unlink(c->local);
    if (c->local_event[0])
        unlink(c->local_event);
    free(c);
}
