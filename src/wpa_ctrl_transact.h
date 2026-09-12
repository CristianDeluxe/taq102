#ifndef WPA_CTRL_TRANSACT_H
#define WPA_CTRL_TRANSACT_H

#include <stddef.h>
int wpa_ctrl_transact(int fd, const char *cmd, char *buf, size_t cap, int timeout_ms);

#endif
