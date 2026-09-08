#!/bin/sh
# Sit and wait for the tablet to appear in loader mode, then flash an image and
# get out of the way. Written because the two-button dance is unreliable on this
# board: the window is in the first second of boot, most attempts miss it, and
# catching the one that lands means somebody watching a terminal.
#
#   tools/loader-watch.sh boot <image>       write it to the boot partition
#   tools/loader-watch.sh recovery <image>   write it to recovery, set the BCB
#   tools/loader-watch.sh bcb                just zero the BCB and reset
#
# It polls every two seconds, logs what it did with a timestamp, and exits.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
RK="$here/vendor/rkdeveloptool/rkdeveloptool"
ZERO=${ZERO:-/Volumes/Datos4TB2/denver-taq102/gate3-build/misc-zero8.img}
WHAT=${1:?usage: loader-watch.sh boot|recovery|bcb [image]}
IMG=${2:-}
TIMEOUT=${TIMEOUT:-7200}

[ "$WHAT" = bcb ] || [ -f "$IMG" ] || { echo "no such image: $IMG" >&2; exit 1; }

echo "$(date +%H:%M:%S) waiting for loader mode; do the button dance whenever you like"
deadline=$(( $(date +%s) + TIMEOUT ))
while [ "$(date +%s)" -lt "$deadline" ]; do
    if "$RK" ld 2>/dev/null | grep -qiE "loader|maskrom"; then
        echo "$(date +%H:%M:%S) caught it: $("$RK" ld 2>/dev/null | head -1)"
        case "$WHAT" in
            boot)     sh "$here/flash-boot.sh" "$IMG" ;;
            recovery) sh "$here/flash-recovery.sh" "$IMG" ;;
            bcb)      "$RK" wl 24608 "$ZERO" && echo "BCB zeroed" && "$RK" rd ;;
        esac
        echo "$(date +%H:%M:%S) done"
        exit 0
    fi
    sleep 2
done
echo "$(date +%H:%M:%S) gave up after ${TIMEOUT}s"
exit 1
