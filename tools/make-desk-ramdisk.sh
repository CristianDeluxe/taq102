#!/bin/sh
# Usage: tools/make-desk-ramdisk.sh <base.cpio.gz> <out.cpio.gz>
# Inject the desk into an existing mainline ramdisk; refuse to overwrite output.
# Build output/dmxdesk if absent (run tools/build-dmxdesk.sh first to refresh an
# existing binary). Keep the base archive byte-for-byte, append a root-owned
# newc overlay, then gzip the combined archives in ONE stream. This preserves
# every unrelated file, mode, symlink, device node, module and the diagnostic
# /init. No extraction or privileged filesystem writes are needed on the host.
# Requires Python 3, cpio with -R uid:gid support, gzip, and git.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
base=${1:?usage: make-desk-ramdisk.sh <base.cpio.gz> <out.cpio.gz>}
out=${2:?usage: make-desk-ramdisk.sh <base.cpio.gz> <out.cpio.gz>}
[ "$#" -eq 2 ] || { echo "expected exactly two arguments" >&2; exit 1; }
[ -f "$base" ] || { echo "base ramdisk is missing: $base" >&2; exit 1; }
[ ! -e "$out" ] && [ ! -L "$out" ] || { echo "refusing to overwrite $out" >&2; exit 1; }
[ -f "$here/output/dmxdesk" ] || "$here/tools/build-dmxdesk.sh"
# The map and its provenance come from the same dmxdesk checkout as the build.
dmxdesk=${DMXDESK_DIR:-$("$here/tools/get-dmxdesk.sh")}
mkdir -p "$here/output"
work=$(mktemp -d "$here/output/desk-ramdisk.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$work/payload/usr/bin" "$work/payload/usr/share/dmxdesk"
cp "$here/output/dmxdesk" "$work/payload/usr/bin/dmxdesk"
cp "$here/br2-external/package/dmxdesk/taq102-desk" "$work/payload/usr/bin/taq102-desk"
cp "$here/br2-external/board/taq102/rootfs-overlay/usr/bin/taq102-app" "$work/payload/usr/bin/taq102-app"
cp "$dmxdesk/show/vibra.desk.json" "$work/payload/usr/share/dmxdesk/vibra.desk.json"
python3 "$dmxdesk/tools/desk-map-version.py" "$dmxdesk" > "$work/payload/usr/share/dmxdesk/VERSION"
chmod 0755 "$work/payload/usr/bin/"* "$work/payload/usr/share/dmxdesk"
chmod 0644 "$work/payload/usr/share/dmxdesk/"*
(
    cd "$work/payload"
    printf '%s\n' usr/share/dmxdesk usr/bin/dmxdesk usr/bin/taq102-desk \
        usr/bin/taq102-app usr/share/dmxdesk/vibra.desk.json usr/share/dmxdesk/VERSION |
        cpio -o -H newc -R 0:0
) > "$work/extra.cpio"
gzip -dc "$base" > "$work/combined.cpio"
cat "$work/extra.cpio" >> "$work/combined.cpio"
gzip -9 < "$work/combined.cpio" > "$work/desk.cpio.gz"
# Exclusive creation also closes the check/write race; never replace an archive.
(set -C; cat "$work/desk.cpio.gz" > "$out")
ls -l "$out"
