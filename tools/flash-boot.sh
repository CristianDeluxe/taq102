#!/bin/sh
# Write an image to the `boot` partition and verify it byte for byte.
#   flash-boot.sh <image>
# Waits for loader mode (reach it with `reboot-loader` over ssh, or both
# buttons held at power-on), writes at the parameter LBA of `boot`, reads the
# same span back, compares SHA-256, and reboots the device. Never write `boot`
# without a bootable image already in `recovery`: the button at power-on is the
# only way back if this one does not come up.
set -eu
here="$(dirname "$0")"
RK="$here/vendor/rkdeveloptool/rkdeveloptool"
IMG="$1"
OFF=131072
size=$(stat -f %z "$IMG")
sectors=$(( (size + 511) / 512 ))
readback="${TMPDIR:-/tmp}/flash-boot-readback.img"

echo "waiting for loader mode..."
i=0
while [ $i -lt 120 ]; do
	if "$RK" ld 2>/dev/null | grep -qi loader; then break; fi
	sleep 1; i=$((i+1))
done
"$RK" ld | grep -qi loader || { echo "no loader after 120 s"; exit 1; }
"$RK" wl $OFF "$IMG"
"$RK" rl $OFF $sectors "$readback"
a=$(shasum -a 256 "$IMG" | cut -d' ' -f1)
b=$(head -c "$size" "$readback" | shasum -a 256 | cut -d' ' -f1)
echo "img=$a"
echo "dev=$b"
if [ "$a" != "$b" ]; then echo "MISMATCH: not rebooting"; exit 1; fi
echo "MATCH"
"$RK" rd
