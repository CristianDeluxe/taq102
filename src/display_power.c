#include "display_power.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int write_power(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) { perror(path); return -1; }
    ssize_t n;
    do { n = write(fd, value, 1); } while (n < 0 && errno == EINTR);
    int result = n == 1 ? 0 : -1;
    if (result) { if (n >= 0) errno = EIO; perror(path); }
    if (close(fd) < 0) { perror(path); result = -1; }
    return result;
}
int display_power_off(void) {
    int light = write_power("/sys/class/backlight/backlight/bl_power", "4");
    int blank = write_power("/sys/class/graphics/fb0/blank", "4");
    return light || blank ? -1 : 0;
}
int display_power_on(void) {
    int light = write_power("/sys/class/backlight/backlight/bl_power", "0");
    int blank = write_power("/sys/class/graphics/fb0/blank", "0");
    return light || blank ? -1 : 0;
}
