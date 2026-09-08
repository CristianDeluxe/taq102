#!/bin/sh
# Recover the tablet after a failed mainline boot, and rescue the crash log.
#
# Run this BEFORE doing the button dance, and leave it running. It waits for
# the device to appear, then does the two time-critical things in order:
#
#   1. Zero the bootloader control block at LBA 24608, so U-Boot stops booting
#      `recovery` (the mainline image) and goes back to `boot` (v49).
#   2. The moment ssh answers, dump the mainline ramoops region. This is a
#      race worth winning: 0x68100000 is ordinary RAM to the vendor kernel, so
#      whatever the mainline kernel logged there is overwritten within seconds
#      of the vendor kernel allocating over it.
#
# The button dance, from I: unplug USB, hold power ~10 s until the
# panel goes dark, then hold both buttons and plug USB in while holding them
# for another 10-15 s. A white screen means that pass failed; unplug and retry.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
RK="$here/vendor/rkdeveloptool/rkdeveloptool"
ZERO=${ZERO:-/Volumes/Datos4TB2/denver-taq102/gate3-build/misc-zero8.img}
OUT=${OUT:-.}
IP=${IP:-192.168.1.51}
KEY=${KEY:-$HOME/.ssh/taq102}
BCB_LBA=24608
RAMOOPS=0x68100000
DEADLINE=$(( $(date +%s) + ${1:-3600} ))

echo "waiting for loader mode (deadline ${1:-3600}s). Do the button dance now."
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
    if "$RK" ld 2>/dev/null | grep -qiE "loader|maskrom"; then
        echo "device found: $("$RK" ld 2>/dev/null | head -1)"
        "$RK" wl "$BCB_LBA" "$ZERO" >/dev/null && echo "BCB zeroed"
        "$RK" rl "$BCB_LBA" 8 "$OUT/bcb-readback.img" >/dev/null 2>&1 &&
            echo "BCB reads back: $(xxd -l 16 -p "$OUT/bcb-readback.img")"
        "$RK" rd >/dev/null && echo "reset; the tablet should come up on v49"
        break
    fi
    sleep 2
done

echo "waiting for ssh, to grab the crash log before it is overwritten"
SSH="ssh -i $KEY -o ConnectTimeout=4 -o BatchMode=yes -o StrictHostKeyChecking=accept-new root@$IP"
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
    if $SSH true 2>/dev/null; then
        $SSH "dd if=/dev/mem bs=4096 skip=\$(($RAMOOPS/4096)) count=240 2>/dev/null" \
            > "$OUT/ramoops-mainline.bin"
        echo "dumped $(wc -c < "$OUT/ramoops-mainline.bin") bytes to $OUT/ramoops-mainline.bin"
        echo "decode it with: tools/read-ramoops.py $OUT/ramoops-mainline.bin"
        exit 0
    fi
    sleep 2
done
echo "deadline reached"
exit 1
