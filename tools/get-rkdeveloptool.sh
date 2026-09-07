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
# Pinned to the commit that was built and used against this tablet
# (master of 2025-03-07); a moving master is a build that may stop working
# the day it is needed most.
RKDEVELOPTOOL_COMMIT=304f073752fd25c854e1bcf05d8e7f925b1f4e14
[ -d rkdeveloptool ] ||
	git clone https://github.com/rockchip-linux/rkdeveloptool.git

cd rkdeveloptool
git checkout -q "$RKDEVELOPTOOL_COMMIT"
autoreconf -i
./configure
make CXXFLAGS="-g -O2 -Wno-vla-cxx-extension -Wno-error"

./rkdeveloptool ld
