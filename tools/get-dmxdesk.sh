#!/bin/sh
# Check out spectalive/dmxdesk at the tag the Buildroot package pins, into
# tools/vendor/dmxdesk/, with its cJSON fetched, for the scripts that build and
# push the desk outside Buildroot. The tag is read from dmxdesk.mk, so the two
# builds cannot drift apart. Prints the checkout's path.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^DMXDESK_VERSION = //p' "$here/br2-external/package/dmxdesk/dmxdesk.mk")
dest=$here/tools/vendor/dmxdesk
[ -n "$version" ] || { echo "no DMXDESK_VERSION in dmxdesk.mk" >&2; exit 1; }
if [ -d "$dest/.git" ]; then
    git -C "$dest" fetch -q --tags origin
else
    git clone -q https://github.com/spectalive/dmxdesk.git "$dest"
fi
git -C "$dest" -c advice.detachedHead=false checkout -q "$version"
[ -f "$dest/tools/vendor/cjson/cJSON.c" ] || "$dest/tools/get-cjson.sh" >&2
echo "$dest"
