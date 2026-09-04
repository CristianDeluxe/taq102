#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include "accel.h"

#define BUS "/dev/i2c-2"
#define ADDR 0x18
#define WHO_AM_I 0x0F     // reads 0x11 on this part
#define CTRL_REG1 0x20
#define CTRL_REG4 0x23
#define OUT_X_L 0x28

static int xfer(int fd, uint8_t *wr, int wn, uint8_t *rd, int rn) {
    struct i2c_msg m[2] = {
        { ADDR, 0, (uint16_t)wn, wr },
        { ADDR, I2C_M_RD, (uint16_t)rn, rd },
    };
    struct i2c_rdwr_ioctl_data d = { m, rn ? 2 : 1 };
    return ioctl(fd, I2C_RDWR, &d) < 0 ? -1 : 0;
}

int accel_open(void) {
    int fd = open(BUS, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    uint8_t reg = WHO_AM_I, id = 0;
    if (xfer(fd, &reg, 1, &id, 1) || id != 0x11) { close(fd); return -1; }
    uint8_t on[2] = { CTRL_REG1, 0x47 };    // 50 Hz, X Y Z enabled
    uint8_t hr[2] = { CTRL_REG4, 0x88 };    // block update, high resolution, +-2 g
    if (xfer(fd, on, 2, NULL, 0) || xfer(fd, hr, 2, NULL, 0)) { close(fd); return -1; }
    return fd;
}

int accel_read(int fd, int *x_mg, int *y_mg, int *z_mg) {
    uint8_t reg = OUT_X_L | 0x80;           // auto-increment
    uint8_t v[6];
    if (xfer(fd, &reg, 1, v, 6)) return -1;
    // 12-bit left-justified two's complement, 1 mg per LSB at +-2 g.
    *x_mg = (int16_t)(v[0] | v[1] << 8) >> 4;
    *y_mg = (int16_t)(v[2] | v[3] << 8) >> 4;
    *z_mg = (int16_t)(v[4] | v[5] << 8) >> 4;
    return 0;
}
