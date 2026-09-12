#!/bin/sh
# Cross-build the lighting desk for the tablet, without rebuilding the image.
#
# The Buildroot toolchain and sysroot live in the OrbStack machine; the sources
# are read from this Mac over the shared mount. Output lands in the machine and
# is copied back here, so it can be pushed to the tablet with
# tools/run-dmxdesk.sh while the image it will eventually ship in is still
# being cooked.
#
# cJSON is compiled straight into the binary rather than added to the image:
# one fewer moving part while the desk is changing every hour. When it settles,
# it becomes a Buildroot package like the others.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
machine=${ORB_MACHINE:-taq102}
out=${DMXDESK_OUT:-$here/output}

[ -f "$here/tools/vendor/cjson/cJSON.c" ] || "$here/tools/get-cjson.sh"

mkdir -p "$out"
orb -m "$machine" -u root bash -lc "
set -eu
SRC=$here/src
VENDOR=$here/tools/vendor/cjson
BUILD=/work/dmxdesk
SYSROOT=/work/output/host/arm-buildroot-linux-gnueabihf/sysroot
CC=/work/output/host/bin/arm-buildroot-linux-gnueabihf-gcc

mkdir -p \$BUILD/include/cjson
cp \$VENDOR/cJSON.h \$BUILD/include/cjson/

# Third party on its own terms; ours under -Werror.
\$CC -O2 -w -I\$SYSROOT/usr/include -c \$VENDOR/cJSON.c -o \$BUILD/cJSON.o

\$CC -std=gnu99 -Wall -Wextra -Werror -O2 \
    -I\$SRC -I\$BUILD/include -I\$SYSROOT/usr/include -I\$SYSROOT/usr/include/libdrm \
    -o \$BUILD/dmxdesk \
    \$SRC/dmxdesk.c \$SRC/desk_model.c \$SRC/desk_paint.c \$SRC/desk_present_drm.c \
    \$SRC/showmap.c \$SRC/showmap_validate.c \$SRC/vcjson.c \$SRC/qlc_codec.c \
    \$SRC/ws_client.c \$SRC/http_get.c \$SRC/canvas.c \$SRC/canvas_blend.c \
    \$SRC/font.c \$SRC/touch_input.c \$SRC/touch_flip.c \$SRC/oneeuro.c \
    \$BUILD/cJSON.o \
    -L\$SYSROOT/usr/lib -ldrm -lm
\$CC -v 2>&1 | tail -1
cp \$BUILD/dmxdesk $out/dmxdesk
"
ls -l "$out/dmxdesk"
file "$out/dmxdesk" 2>/dev/null || true
