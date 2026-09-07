#include "power_key.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

int power_key_read(struct power_key *key, int fd) {
    int pressed = 0;
    while (fd >= 0) {
        ssize_t n = read(fd, key->bytes + key->used, sizeof key->bytes - key->used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        key->used += (size_t)n;
        if (key->used == sizeof key->bytes) {
            uint16_t type, code;
            int32_t value;
            memcpy(&type, key->bytes + 8, 2);
            memcpy(&code, key->bytes + 10, 2);
            memcpy(&value, key->bytes + 12, 4);
            if (type == EV_KEY && code == KEY_POWER && value == 1) pressed = 1;
            key->used = 0;
        }
    }
    return pressed;
}
