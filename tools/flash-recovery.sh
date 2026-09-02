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

RKDEVELOPTOOL=${RKDEVELOPTOOL:?set RKDEVELOPTOOL to the built binary}
IMG=${1:?usage: flash-recovery.sh <recovery.img>}

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

BCB=$(mktemp)
printf 'boot-recovery' > "$BCB"
dd if=/dev/zero bs=1 count=$((4096 - 13)) >> "$BCB" 2>/dev/null
"$RKDEVELOPTOOL" wl "$BCB_LBA" "$BCB"
rm -f "$BCB"

"$RKDEVELOPTOOL" rd
