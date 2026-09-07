// SOURCES: action_worker.c wifi_status.c reboot_target.c
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "action_worker.h"
#include "wifi_status.h"
#include "reboot_target.h"

static void finish(struct action_worker *w, int expected) {
    int st = 999, done = 0;
    for (int i = 0; i < 300 && !done; ++i) {
        done = aw_poll(w, &st);
        if (!done) usleep(10000);
    }
    assert(done == 1 && st == expected && !aw_busy(w));
    assert(aw_poll(w, &st) == 0);
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "fd") == 0)
        return fcntl(atoi(argv[2]), F_GETFD) == -1 ? 0 : 1;
    struct action_worker *w = aw_new(); assert(w);
    const char *ok[] = { "/bin/sh", "-c", "exit 3", NULL };
    assert(aw_start(w, ok, 5) == 0);
    assert(aw_start(w, ok, 5) == -1);
    finish(w, 3);
    const char *missing[] = { "/definitely/missing", NULL };
    assert(aw_start(w, missing, 5) == 0); finish(w, 127);
    int fd = open("/dev/null", O_RDONLY); assert(fd >= 0);
    int high = fcntl(fd, F_DUPFD, 512); assert(high >= 512);
    char number[32]; snprintf(number, sizeof number, "%d", high);
    const char *check[] = { argv[0], "fd", number, NULL };
    assert(aw_start(w, check, 5) == 0); finish(w, 0);
    close(fd); close(high);
    char dir[] = "/tmp/taq102-audit/actions-XXXXXX"; assert(mkdtemp(dir));
    char p[512], command[1024];
    snprintf(p, sizeof p, "%s/survivor", dir);
    snprintf(command, sizeof command, "(sleep 2; touch '%s') & wait", p);
    const char *slow[] = { "/bin/sh", "-c", command, NULL };
    assert(aw_start(w, slow, 1) == 0);
    sleep(3); /* The timeout must run while the UI is not polling. */
    assert(access(p, F_OK) == -1);
    assert(aw_start(w, ok, 5) == -1); /* Completion cannot be overwritten. */
    finish(w, -1);
    assert(aw_start(w, ok, 5) == 0); finish(w, 3);
    assert(aw_start(w, slow, 10) == 0); aw_free(w);

    snprintf(p, sizeof p, "%s/wifi.conf", dir);
    FILE *f = fopen(p, "w"); assert(f);
    fputs("network={\n\tpsk=secret\n\tssid=\"Deluxe\\\"Wifi\" # comment\n}\n", f); fclose(f);
    char ssid[33]; assert(wifi_read_ssid(p, ssid, sizeof ssid) == 0);
    assert(strcmp(ssid, "Deluxe\"Wifi") == 0);
    assert(wifi_read_ssid(p, ssid, 2) == -1 && ssid[0] == '\0');
    assert(wifi_read_ssid("/nonexistent", ssid, sizeof ssid) == -1 && !ssid[0]);
    f = fopen(p, "w"); assert(f); fputs("ssid=\"unterminated\npsk=secret\n", f); fclose(f);
    assert(wifi_read_ssid(p, ssid, sizeof ssid) == -1 && !ssid[0]);
    f = fopen(p, "w"); assert(f); fputs("ssid=\"name\"garbage\n", f); fclose(f);
    assert(wifi_read_ssid(p, ssid, sizeof ssid) == -1 && !ssid[0]);
    struct status status = {0};
    assert(wifi_state_from(&status, 0, 0, 0) == WIFI_OFF);
    assert(wifi_state_from(&status, 1, 0, 1) == WIFI_STARTING);
    assert(wifi_state_from(&status, 0, 1, 1) == WIFI_FAILED);
    assert(wifi_state_from(&status, 0, 1, 0) == WIFI_FAILED);
    status.have_wifi = 1; strcpy(status.addr, "NO WIFI YET");
    assert(wifi_state_from(&status, 0, 0, 1) == WIFI_ASSOCIATING);
    strcpy(status.addr, "192.168.1.2");
    assert(wifi_state_from(&status, 0, 0, 1) == WIFI_CONNECTED);
    strcpy(status.addr, "0.0.0.0");
    assert(wifi_state_from(&status, 0, 0, 1) == WIFI_ASSOCIATING);

    snprintf(p, sizeof p, "%s/emmc.img", dir);
    unsigned char bytes[5120]; memset(bytes, 0xa5, sizeof bytes);
    f = fopen(p, "wb"); assert(f); assert(fwrite(bytes, 1, sizeof bytes, f) == sizeof bytes); fclose(f);
    char err[128]; assert(bcb_write_verify(p, 1, err, sizeof err) == 0);
    f = fopen(p, "rb"); assert(f); assert(fread(bytes, 1, sizeof bytes, f) == sizeof bytes); fclose(f);
    for (size_t i = 0; i < sizeof bytes; ++i) {
        unsigned char expected = i < 512 || i >= 4608 ? 0xa5 : 0;
        if (i >= 512 && i < 525) expected = (unsigned char)"boot-recovery"[i - 512];
        assert(bytes[i] == expected);
    }
    assert(bcb_write_verify(p, -1, err, sizeof err) == -1 && err[0]);
    assert(bcb_write_verify(p, 3, err, sizeof err) == -1 && err[0]);
    struct stat info; assert(stat(p, &info) == 0 && info.st_size == 5120);
    unsigned char unchanged[sizeof bytes];
    f = fopen(p, "rb"); assert(f);
    assert(fread(unchanged, 1, sizeof unchanged, f) == sizeof unchanged); fclose(f);
    assert(memcmp(bytes, unchanged, sizeof bytes) == 0);
    assert(bcb_write_verify("/nonexistent/dev", 32800, err, sizeof err) == -1 && err[0]);
    snprintf(p, sizeof p, "%s/emmc-rescue.img", dir);
    fd = open(p, O_RDWR | O_CREAT | O_EXCL, 0600); assert(fd >= 0);
    off_t rescue_offset = 32800L * 512;
    assert(ftruncate(fd, rescue_offset + 4608) == 0);
    memset(bytes, 0xa5, sizeof bytes);
    assert(pwrite(fd, bytes, sizeof bytes, rescue_offset - 512) == sizeof bytes);
    assert(bcb_write_verify(p, 32800, err, sizeof err) == 0);
    assert(pread(fd, bytes, sizeof bytes, rescue_offset - 512) == sizeof bytes);
    close(fd);
    for (size_t i = 0; i < sizeof bytes; ++i) {
        unsigned char expected = i < 512 || i >= 4608 ? 0xa5 : 0;
        if (i >= 512 && i < 525) expected = (unsigned char)"boot-recovery"[i - 512];
        assert(bytes[i] == expected);
    }
    char root[512], entry[1024], start[1100], dev[512];
    snprintf(root, sizeof root, "%s/sys", dir); assert(mkdir(root, 0700) == 0);
    snprintf(entry, sizeof entry, "%s/mmcblk12p3", root); assert(mkdir(entry, 0700) == 0);
    snprintf(start, sizeof start, "%s/start", entry);
    f = fopen(start, "w"); assert(f); fputs("4867072\n", f); fclose(f);
    assert(emmc_device_at(root, dir, dev, sizeof dev) == 0);
    snprintf(p, sizeof p, "%s/mmcblk12", dir); assert(strcmp(dev, p) == 0);
    assert(emmc_device_at(root, dir, dev, 2) == -1 && !dev[0]);
    snprintf(entry, sizeof entry, "%s/mmcblk2p1", root); assert(mkdir(entry, 0700) == 0);
    snprintf(start, sizeof start, "%s/start", entry);
    f = fopen(start, "w"); assert(f); fputs("4867072\n", f); fclose(f);
    assert(emmc_device_at(root, dir, dev, sizeof dev) == -1 && !dev[0]);
    puts("actions ok"); return 0;
}
