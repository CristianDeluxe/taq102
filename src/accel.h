// The Silan SC7A20 accelerometer on i2c-2 at 0x18, a LIS3DH-compatible part
// that the vendor kernel never drove (its sensor framework looks for an
// STK8BAxx there and finds no driver built). Read directly over /dev/i2c-2.
#ifndef ACCEL_H
#define ACCEL_H

// Open the bus, wake the chip at 50 Hz, high resolution, +-2 g. -1 if absent.
int accel_open(void);
// One sample in milli-g per axis. Non-zero on a bus error.
int accel_read(int fd, int *x_mg, int *y_mg, int *z_mg);

#endif
