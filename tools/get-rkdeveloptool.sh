#!/bin/sh
# Build rkdeveloptool for macOS into tools/vendor/.
#
# It is not in Homebrew, and its master does not build under Clang: two
# -Wvla-cxx-extension errors, fatal because the Makefile carries -Werror.
# Demoting that one warning is the whole patch; version 1.32 then talks to the
# device with no other change.
#
# Needs libusb, autoconf, automake and libtool from Homebrew.
set -eu

VENDOR=$(cd "$(dirname "$0")/vendor" 2>/dev/null && pwd || {
	mkdir -p "$(dirname "$0")/vendor"
	cd "$(dirname "$0")/vendor" && pwd
})

cd "$VENDOR"
[ -d rkdeveloptool ] ||
	git clone --depth 1 https://github.com/rockchip-linux/rkdeveloptool.git

cd rkdeveloptool
autoreconf -i
./configure
make CXXFLAGS="-g -O2 -Wno-vla-cxx-extension -Wno-error"

./rkdeveloptool ld
