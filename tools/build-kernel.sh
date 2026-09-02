#!/bin/sh
# Build the vendor 4.4.167 kernel and this board's DTB, inside the Linux VM.
#
# Run this in the OrbStack machine `taq102`, not on macOS: it needs the
# Buildroot host toolchain in /work/output/host/bin, which only exists there.
#
# Seven things in this tree or its host break a modern build, and every one of
# them fails in a way that looks like something else. They are handled below
# rather than explained at the call site:
#
#   1. `sudo -E` is ignored, so ARCH and CROSS_COMPILE must be passed as make
#      arguments or the build silently targets the VM's own arm64 and stops at
#      "No rule to make target 'zImage'".
#   2. scripts/ needs a `python` on PATH; this tree predates python3-only hosts.
#   3. scripts/dtc does not link without -fcommon (GCC 10+ defaults to
#      -fno-common): "multiple definition of `yylloc'".
#   4. scripts/gcc-wrapper.py is Rockchip's own wrapper. It deletes the object
#      and aborts on any warning outside a whitelist written for GCC 6, so
#      "error, forbidden warning:" is not a compiler error. Overriding CC on
#      the make line bypasses it without patching the tree.
#   5. Warnings this GCC adds and that one did not are demoted with KCFLAGS.
#   6. binutils >= 2.36 rejects `.section ".proc.info.init", #alloc`; the flags
#      are now quoted strings. 33 sites in arch/arm.
#   7. drivers/media/i2c/sc031gs.c references
#      v4l2_async_register_subdev_sensor_common, which does not exist in this
#      tree -- the driver is broken as shipped and `default y` in its Kconfig
#      puts it back on every olddefconfig, so it is dropped from the Makefile.
#
# Output: arch/arm/boot/zImage and arch/arm/boot/dts/rk3126-taq102.dtb.
set -eu

KERNEL=${KERNEL_DIR:-/work/kernel}
HOSTBIN=${HOSTBIN:-/work/output/host/bin}
CROSS=$HOSTBIN/arm-buildroot-linux-gnueabihf-
DTS=${DTS:-$HOME/p/taq102/kernel/rk3126-taq102.dts}
CONFIG=${CONFIG:?set CONFIG to the kernel .config to build from}
JOBS=${JOBS:-$(nproc)}

# One line on purpose: mkcompile_h embeds this verbatim in compile.h, and a
# newline inside it ends the build with "Syntax error: Unterminated quoted
# string" from a shell that is nowhere near the kernel source.
WARNINGS="-Wno-error -Wno-array-bounds -Wno-stringop-overflow -Wno-stringop-truncation -Wno-maybe-uninitialized -Wno-attribute-alias -Wno-misleading-indentation -Wno-dangling-pointer -Wno-use-after-free"

[ -x /usr/bin/python ] || ln -sf /usr/bin/python3 /usr/bin/python
command -v lzop >/dev/null || apt-get install -y -qq lzop

cd "$KERNEL"

cp "$DTS" arch/arm/boot/dts/rk3126-taq102.dts
grep -q rk3126-taq102.dtb arch/arm/boot/dts/Makefile ||
	sed -i '/rk3126-bnd-d708.dtb/i\\trk3126-taq102.dtb \\' arch/arm/boot/dts/Makefile

# Quoted section flags, in place of the pre-2.36 `#alloc` spelling.
find arch/arm -name '*.S' -exec sed -i \
	-e 's/,\s*#alloc\s*,\s*#execinstr/, "ax"/g' \
	-e 's/,\s*#alloc\s*,\s*#write/, "aw"/g' \
	-e 's/,\s*#alloc\b/, "a"/g' {} +

sed -i 's|^obj-$(CONFIG_VIDEO_SC031GS)|#obj-$(CONFIG_VIDEO_SC031GS)|' \
	drivers/media/i2c/Makefile

cp "$CONFIG" .config
make ARCH=arm CROSS_COMPILE="$CROSS" CC="${CROSS}gcc" \
	HOSTCFLAGS="-fcommon -w" olddefconfig

# shellcheck disable=SC2086
make ARCH=arm CROSS_COMPILE="$CROSS" CC="${CROSS}gcc" \
	HOSTCFLAGS="-fcommon -w" KCFLAGS="$WARNINGS" \
	-j"$JOBS" zImage rk3126-taq102.dtb

ls -l arch/arm/boot/zImage arch/arm/boot/dts/rk3126-taq102.dtb
