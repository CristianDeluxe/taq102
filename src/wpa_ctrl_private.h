#ifndef WPA_CTRL_PRIVATE_H
#define WPA_CTRL_PRIVATE_H

struct wpa_ctrl {
    int fd, event_fd;
    char local[108], local_event[108];
    char path[108];
    int generation;
    int pending;
    char directory[108];
};

#endif
