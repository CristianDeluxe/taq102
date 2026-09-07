#!/bin/sh
# Write a recovery image and tell U-Boot to boot it. Destructive, by design.
#
# Two rules are encoded here and must not be relaxed:
#   * Sectors are addressed by LBA from the host. Partition numbers differ
#     between Android and our own system on the same kernel, which is how 20 KB
#     of `trust` got zeroed once already.
#   * The bootloader control block lives at `misc` + 16 KB, i.e. LBA 24608, not
#     at the AOSP offset 0. With the BCB at the partition start U-Boot ignores
#     it and boots Android.
#
# Nothing may boot Android between this write and the boot that follows it:
# /system/bin/install-recovery.sh restores the stock recovery image from
# recovery-from-boot.p on every normal Android boot.
set -eu

RECOVERY_LBA=196608     # recovery partition start
RECOVERY_SECTORS=131072 # 64 MB
BCB_LBA=24608           # misc + 16 KB

here=$(dirname "$0")
RKDEVELOPTOOL=${RKDEVELOPTOOL:-$here/vendor/rkdeveloptool/rkdeveloptool}

# --no-bcb writes and verifies `recovery` and leaves the bootloader control
# block alone, so the appliance in `boot` keeps running: the v43 refresh of
# 2026-09-05 needed exactly that and had to call rkdeveloptool by hand.
# Without it the BCB is set and the device reboots into what was written,
# which is what testing a rescue image wants.
SET_BCB=yes
if [ "${1:-}" = "--no-bcb" ]; then SET_BCB=no; shift; fi
IMG=${1:?usage: flash-recovery.sh [--no-bcb] <recovery.img>}
[ -x "$RKDEVELOPTOOL" ] || { echo "no rkdeveloptool at $RKDEVELOPTOOL; run tools/get-rkdeveloptool.sh" >&2; exit 1; }

SIZE=$(wc -c < "$IMG")
if [ "$SIZE" -gt $((RECOVERY_SECTORS * 512)) ]; then
    echo "image is $SIZE bytes, larger than the recovery partition" >&2
    exit 1
fi

if ! "$RKDEVELOPTOOL" ld | grep -q Loader; then
    echo "no device in loader mode; run reboot-loader on the tablet" >&2
    exit 1
fi

"$RKDEVELOPTOOL" wl "$RECOVERY_LBA" "$IMG"

# Read the span back and compare before touching the BCB: a short or wrong
# write here is a device that boots into nothing on the next button.
READBACK=$(mktemp)
"$RKDEVELOPTOOL" rl "$RECOVERY_LBA" $(( (SIZE + 511) / 512 )) "$READBACK"
A=$(shasum -a 256 "$IMG" | cut -d' ' -f1)
B=$(head -c "$SIZE" "$READBACK" | shasum -a 256 | cut -d' ' -f1)
rm -f "$READBACK"
if [ "$A" != "$B" ]; then
    echo "readback mismatch: img=$A dev=$B; BCB left alone" >&2
    exit 1
fi
echo "recovery verified $A"

if [ "$SET_BCB" = no ]; then
    echo "BCB left alone; the running system stays in boot"
    exit 0
fi

BCB=$(mktemp)
printf 'boot-recovery' > "$BCB"
dd if=/dev/zero bs=1 count=$((4096 - 13)) >> "$BCB" 2>/dev/null
"$RKDEVELOPTOOL" wl "$BCB_LBA" "$BCB"
rm -f "$BCB"

"$RKDEVELOPTOOL" rd
