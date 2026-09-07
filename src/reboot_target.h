#ifndef REBOOT_TARGET_H
#define REBOOT_TARGET_H
#include <stddef.h>
int bcb_write_verify(const char *dev, long sector, char *err, size_t errn);
int emmc_device(char *out, size_t n);
/* Roots make partition discovery testable without touching real devices. */
int emmc_device_at(const char *sys_block_root, const char *dev_root, char *out, size_t n);
int reboot_target_rescue(char *err, size_t errn);
void reboot_target_loader(void);
#endif
