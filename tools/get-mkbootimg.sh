#!/bin/sh
# Fetch AOSP's mkbootimg.py into tools/vendor/, with the one module it imports.
#
# googlesource serves base64 when asked for a raw file, hence the decode. The
# script imports gki.generate_gki_certificate unconditionally even though a
# header_version 0 image never signs anything, so that file and an empty
# __init__.py have to come along or the import fails.
set -eu

VENDOR=$(dirname "$0")/vendor
# Pinned to a commit rather than main: only header_version 0 is used here and
# the script has no reason to change under us.
MKBOOTIMG_COMMIT=d2bb0af5ba6d3198a3e99529c97eda1be0b5a093
BASE=https://android.googlesource.com/platform/system/tools/mkbootimg/+/$MKBOOTIMG_COMMIT

mkdir -p "$VENDOR/gki"
cd "$VENDOR"

curl -sL "$BASE/mkbootimg.py?format=TEXT" | base64 -d > mkbootimg.py
curl -sL "$BASE/gki/generate_gki_certificate.py?format=TEXT" | base64 -d \
	> gki/generate_gki_certificate.py
: > gki/__init__.py

python3 mkbootimg.py --help > /dev/null && echo "mkbootimg.py ready in $VENDOR"
