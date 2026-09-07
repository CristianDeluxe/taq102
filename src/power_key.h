#ifndef POWER_KEY_H
#define POWER_KEY_H
#include <stddef.h>
/* Linux 4.4 ARM evdev records have 32-bit seconds even with time64 libc. */
struct power_key { unsigned char bytes[16]; size_t used; };
int power_key_read(struct power_key *key, int fd);
#endif
