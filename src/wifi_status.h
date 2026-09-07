#ifndef WIFI_STATUS_H
#define WIFI_STATUS_H
#include <stddef.h>
#include "status.h"
#include "wifi_state.h"
int wifi_read_ssid(const char *conf_path, char *out, size_t n);
enum wifi_state wifi_state_from(const struct status *st, int worker_busy, int last_exit, int wanted_on);
#endif
