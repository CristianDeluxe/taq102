// Reboot into Rockchip loader mode (rockusb), so the host can flash without
// booting Android first.
//
// The vendor kernel's reboot notifier reads the command string and stores a
// magic in the PMU grf register that U-Boot checks on the next start; BusyBox's
// reboot applet cannot pass that string, which is the only reason this exists.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/reboot.h>
#include <linux/reboot.h>

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "loader";
    sync();
    if (syscall(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                LINUX_REBOOT_CMD_RESTART2, cmd) < 0) {
        perror("reboot");
        return 1;
    }
    return 0;
}
