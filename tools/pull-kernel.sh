#!/bin/sh
# Copy the zImage the VM just built onto the Mac, and refuse a short copy.
#   pull-kernel.sh <dest>
# `orb -m taq102 cat zImage > file` truncated a 7.9 MB kernel at a round
# 6,946,816 bytes on 2026-09-03 without an error, and the image built from it
# left the tablet dark at U-Boot with no USB and no network -- one button
# away from a brick. The VM sees the Mac's /Users, so the copy goes through
# that mount and both sides hash the result before anything is packed.
set -eu
dest="$1"
stage="$HOME/p/taq102/log/zImage-pull"
vm_sum=$(orb -m taq102 -u root bash -lc "cp /work/kernel/arch/arm/boot/zImage '$stage' && sha256sum /work/kernel/arch/arm/boot/zImage | cut -d' ' -f1")
mac_sum=$(shasum -a 256 "$stage" | cut -d' ' -f1)
if [ "$vm_sum" != "$mac_sum" ]; then
	echo "pull-kernel: hash mismatch vm=$vm_sum mac=$mac_sum" >&2
	exit 1
fi
mv "$stage" "$dest"
echo "$mac_sum  $dest"
