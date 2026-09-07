#include "reboot_target.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#endif

static int failure(char *err, size_t n, const char *message) {
    if (err && n) snprintf(err, n, "%s", message);
    return -1;
}

int bcb_write_verify(const char *dev, long sector, char *err, size_t errn) {
    if (err && errn) err[0] = '\0';
    if (!dev || sector < 0 || (uintmax_t)sector > (INT64_MAX - 4096) / 512)
        return failure(err, errn, "Invalid BCB sector");
    uint64_t offset = (uint64_t)sector * 512;
    off_t position = (off_t)(offset + 4096);
    if (position < 0 || (uint64_t)position != offset + 4096)
        return failure(err, errn, "BCB offset exceeds platform range");
    int fd = open(dev, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return failure(err, errn, "Cannot open BCB target");
    struct stat st;
    uint64_t extent = 0;
    const char *message = NULL;
    if (fstat(fd, &st) != 0) message = "Cannot inspect BCB target";
    else if (S_ISREG(st.st_mode) && st.st_size >= 0) extent = (uint64_t)st.st_size;
#ifdef __linux__
    else if (S_ISBLK(st.st_mode)) {
        if (ioctl(fd, BLKGETSIZE64, &extent) != 0) message = "Cannot read block device extent";
    }
#endif
    else message = "Unsupported BCB target type";
    if (!message && extent < offset + 4096) message = "BCB target is too small";
    unsigned char expected[4096] = "boot-recovery", actual[4096];
    size_t done = 0;
    while (!message && done < sizeof expected) {
        ssize_t count = pwrite(fd, expected + done, sizeof expected - done, (off_t)(offset + done));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) message = "Cannot write complete BCB";
        else done += (size_t)count;
    }
    if (!message && fsync(fd) != 0) message = "Cannot sync BCB";
    done = 0;
    while (!message && done < sizeof actual) {
        ssize_t count = pread(fd, actual + done, sizeof actual - done, (off_t)(offset + done));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) message = "Cannot read complete BCB verification";
        else done += (size_t)count;
    }
    if (!message && memcmp(expected, actual, sizeof expected)) message = "BCB verification mismatch";
    if (close(fd) != 0 && !message) message = "Cannot close BCB target";
    return message ? failure(err, errn, message) : 0;
}

int emmc_device_at(const char *sys_block_root, const char *dev_root, char *out, size_t n) {
    if (!out || !n) return -1;
    out[0] = '\0';
    if (!sys_block_root || !dev_root) return -1;
    DIR *dir = opendir(sys_block_root);
    if (!dir) return -1;
    char found[NAME_MAX + 1] = "";
    struct dirent *entry;
    int failed = 0;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (!entry) { if (errno) failed = 1; break; }
        const char *name = entry->d_name;
        if (strncmp(name, "mmcblk", 6)) continue;
        const char *p = name + 6;
        if (!isdigit((unsigned char)*p)) continue;
        while (isdigit((unsigned char)*p)) ++p;
        size_t parent_len = (size_t)(p - name);
        if (*p++ != 'p' || !isdigit((unsigned char)*p)) continue;
        while (isdigit((unsigned char)*p)) ++p;
        if (*p) continue;
        char path[PATH_MAX];
        int length = snprintf(path, sizeof path, "%s/%s/start", sys_block_root, name);
        if (length < 0 || (size_t)length >= sizeof path) { failed = 1; break; }
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) { failed = 1; break; }
        char value[64];
        ssize_t count;
        do { count = read(fd, value, sizeof value - 1); } while (count < 0 && errno == EINTR);
        close(fd);
        if (count <= 0 || count == (ssize_t)sizeof value - 1) { failed = 1; break; }
        value[count] = '\0';
        unsigned long long start; int consumed = 0;
        if (sscanf(value, "%llu%n", &start, &consumed) != 1) { failed = 1; break; }
        p = value + consumed;
        while (isspace((unsigned char)*p)) ++p;
        if (*p) { failed = 1; break; }
        if (start != 4867072) continue;
        if (found[0]) { failed = 1; break; }
        memcpy(found, name, parent_len); found[parent_len] = '\0';
    }
    closedir(dir);
    if (failed || !found[0]) return -1;
    int length = snprintf(out, n, "%s/%s", dev_root, found);
    if (length < 0 || (size_t)length >= n) { out[0] = '\0'; return -1; }
    return 0;
}

int emmc_device(char *out, size_t n) {
    return emmc_device_at("/sys/class/block", "/dev", out, n);
}

int reboot_target_rescue(char *err, size_t errn) {
#ifdef __linux__
    char dev[PATH_MAX];
    if (emmc_device(dev, sizeof dev) != 0)
        return failure(err, errn, "Cannot resolve one eMMC rescue target");
    if (bcb_write_verify(dev, 32800, err, errn) != 0) return -1;
    if (reboot(RB_AUTOBOOT) != 0) return failure(err, errn, "Reboot failed after verified BCB write");
    return failure(err, errn, "Reboot unexpectedly returned");
#else
    errno = ENOTSUP;
    return failure(err, errn, "Rescue reboot is supported only on Linux");
#endif
}

void reboot_target_loader(void) {
    char *const argv[] = { "/usr/sbin/reboot-loader", NULL };
    execv(argv[0], argv);
}
