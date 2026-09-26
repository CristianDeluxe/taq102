#!/bin/sh
# Cross-build the desk with the existing OrbStack Buildroot toolchain.
# The source is spectalive/dmxdesk at the tag the Buildroot dmxdesk package
# pins (tools/get-dmxdesk.sh), or DMXDESK_DIR for a local checkout; its
# src/dmxdesk.sources is the file list both builds compile. Both compile the
# pinned cJSON into the executable, keeping today's hand-assembled ramdisks
# independent of a new shared library.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
machine=${ORB_MACHINE:-taq102}
out=${DMXDESK_OUT:-$here/output}
dmxdesk=${DMXDESK_DIR:-$("$here/tools/get-dmxdesk.sh")}
dmxdesk=$(cd "$dmxdesk" && pwd)
[ -f "$dmxdesk/tools/vendor/cjson/cJSON.c" ] || "$dmxdesk/tools/get-cjson.sh"
mkdir -p "$out"
out=$(cd "$out" && pwd)
orb -m "$machine" -u root bash -s -- "$dmxdesk" "$out" <<'BUILD'
set -eu
dmxdesk=$1
out=$2
SRC=$dmxdesk/src
VENDOR=$dmxdesk/tools/vendor/cjson
BUILD=$out/build
export TMPDIR=$out/tmp
SYSROOT=/work/output/host/arm-buildroot-linux-gnueabihf/sysroot
CC=/work/output/host/bin/arm-buildroot-linux-gnueabihf-gcc
mkdir -p "$BUILD/include/cjson" "$TMPDIR"
cp "$VENDOR/cJSON.h" "$BUILD/include/cjson/"
# Third-party code on its own terms; project code under -Werror.
"$CC" -O2 -w -I"$SYSROOT/usr/include" -c "$VENDOR/cJSON.c" -o "$BUILD/cJSON.o"
set --
while IFS= read -r source; do
    [ -n "$source" ] && set -- "$@" "$SRC/$source"
done < "$SRC/dmxdesk.sources"
"$CC" -std=gnu99 -Wall -Wextra -Werror -O2 \
    -I"$SRC" -I"$BUILD/include" -I"$SYSROOT/usr/include" -I"$SYSROOT/usr/include/libdrm" \
    -o "$BUILD/dmxdesk" "$@" "$BUILD/cJSON.o" -L"$SYSROOT/usr/lib" -ldrm -lm -pthread
"$CC" -v 2>&1 | tail -1
cp "$BUILD/dmxdesk" "$out/dmxdesk"
BUILD
ls -l "$out/dmxdesk"
file "$out/dmxdesk"
