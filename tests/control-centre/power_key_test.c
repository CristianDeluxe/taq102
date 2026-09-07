// SOURCES: power_key.c
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include "power_key.h"
int main(void) {
    int fds[2]; assert(pipe(fds) == 0);
    assert(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
    unsigned char wire[16] = {0};
    uint16_t type = EV_KEY, code = KEY_POWER;
    int32_t value = 1;
    memcpy(wire+8, &type, 2); memcpy(wire+10, &code, 2); memcpy(wire+12, &value, 4);
    struct power_key key = {0};
    assert(write(fds[1], wire, 7) == 7);
    assert(!power_key_read(&key, fds[0]));
    assert(write(fds[1], wire+7, 9) == 9);
    assert(power_key_read(&key, fds[0]));
    assert(!power_key_read(&key, fds[0]));
    value = 0; memcpy(wire+12, &value, 4);
    assert(write(fds[1], wire, 16) == 16 && !power_key_read(&key, fds[0]));
    value = 2; memcpy(wire+12, &value, 4);
    assert(write(fds[1], wire, 16) == 16 && !power_key_read(&key, fds[0]));
    assert(!power_key_read(&key, -1));
    close(fds[0]); close(fds[1]);
    puts("power_key ok"); return 0;
}
