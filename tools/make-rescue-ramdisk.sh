#!/bin/sh
# Turn the normal ramdisk into the rescue variant, without a second build.
#
# Takes the *uncompressed* rootfs.cpio, appends a two-file archive and
# compresses the pair once. Appending a second gzip *stream* to the finished
# rootfs.cpio.gz does not work on this 4.4 kernel -- it unpacks the first
# member and silently ignores the rest, which looks exactly like the marker
# files never having been added. Inside one stream the unpacker walks every
# archive, and later entries win.
set -eu

IN=${1:?usage: make-rescue-ramdisk.sh <rootfs.cpio> <out.cpio.gz>}
OUT=${2:?usage: make-rescue-ramdisk.sh <rootfs.cpio> <out.cpio.gz>}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/etc"
: > "$WORK/etc/taq102-no-autostart"
echo rescue > "$WORK/etc/taq102-variant"

(cd "$WORK" && find etc -print | cpio -o -H newc --quiet) > "$WORK/extra.cpio"
cat "$IN" "$WORK/extra.cpio" | gzip -9 > "$OUT"

ls -l "$OUT"
