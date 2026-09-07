// SOURCES: status.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "status.h"

static void write_value(const char *path, const char *value) {
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs(value, file) >= 0);
    assert(fclose(file) == 0);
}

int main(void) {
    struct status status;
    char root[] = "/tmp/taq102-audit/power-XXXXXX";
    char usb[256], ac[256], battery[256], path[256];
    memset(&status, 0x55, sizeof status);
    assert(mkdtemp(root) != NULL);
    snprintf(usb, sizeof usb, "%s/usb", root);
    snprintf(ac, sizeof ac, "%s/ac", root);
    snprintf(battery, sizeof battery, "%s/battery", root);
    assert(mkdir(usb, 0700) == 0 && mkdir(ac, 0700) == 0 && mkdir(battery, 0700) == 0);
#define WRITE(relative, value) do { \
    snprintf(path, sizeof path, "%s/%s", root, relative); \
    write_value(path, value); \
} while (0)
    WRITE("usb/online", "1\n");
    WRITE("ac/online", "0\n");
    WRITE("battery/current_now", "250000\n");
    WRITE("battery/capacity", "83\n");
    WRITE("battery/voltage_now", "4123000\n");
    WRITE("battery/status", "Charging\n");
    status_power_read_at(&status, root, 1234567890123LL);
    assert(status.usb_valid && status.ac_valid && status.online_valid);
    assert(status.usb_online == 1 && status.ac_online == 0 && status.plugged == 1);
    assert(status.current_valid && status.current_ua == 250000 && status.ma == 250);
    assert(status.cap_valid && status.cap == 83 && status.have_batt);
    assert(status.word_valid && strcmp(status.word, "Charging") == 0);
    assert(status.mv == 4123 && status.read_ms == 1234567890123LL);

    WRITE("usb/online", "invalid\n");
    WRITE("battery/current_now", "12x\n");
    WRITE("battery/capacity", "101\n");
    snprintf(path, sizeof path, "%s/ac/online", root);
    assert(remove(path) == 0);
    status_power_read_at(&status, root, 8);
    assert(!status.usb_valid && !status.ac_valid && !status.online_valid);
    assert(status.plugged == 1);
    assert(!status.current_valid && !status.cap_valid && !status.have_batt);
    puts("status_power ok");
    return 0;
}
