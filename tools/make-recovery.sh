#!/bin/sh
# Pack a Buildroot rootfs.cpio.gz into a recovery image the stock U-Boot boots.
#
# The kernel and the `second` blob come out of the stock recovery image and are
# reused verbatim unless KERNEL= and SECOND= point at our own zImage and
# resource image: this replaces the ramdisk and nothing else. Every offset
# below was read from the stock image with unpackbootimg; U-Boot's
# "boot or recovery image sha mismatch!" is an internal check that mkbootimg
# satisfies, so an unsigned image is accepted.
set -eu

ARCHIVE=${TAQ102_ARCHIVE:-/Volumes/Datos4TB2/denver-taq102}
MKBOOTIMG=${MKBOOTIMG:?set MKBOOTIMG to the path of mkbootimg.py}

RAMDISK=${1:?usage: make-recovery.sh <rootfs.cpio.gz> <out.img>}
OUT=${2:?usage: make-recovery.sh <rootfs.cpio.gz> <out.img>}

python3 "$MKBOOTIMG" \
    --header_version 0 --os_version 8.1.0 --os_patch_level 2019-06 \
    --kernel "${KERNEL:-$ARCHIVE/gate3-build/kernel}" \
    --ramdisk "$RAMDISK" \
    --second "${SECOND:-$ARCHIVE/gate3-build/second}" \
    --pagesize 0x00000800 --base 0x00000000 \
    --kernel_offset 0x10008000 --ramdisk_offset 0x11000000 \
    --second_offset 0x10f00000 --tags_offset 0x10000100 \
    --board '' --cmdline buildvariant=user \
    -o "$OUT"

ls -l "$OUT"
