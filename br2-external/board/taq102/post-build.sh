#!/bin/sh
# Stamp the image with a build id so a booted system can be identified with
# certainty, and make sure our /init is the one the kernel will run.
set -e
TARGET_DIR="$1"
BUILD_ID="$(date -u +%Y%m%d-%H%M%S)-$(git -C "$BR2_EXTERNAL_TAQ102_PATH" rev-parse --short HEAD 2>/dev/null || echo nogit)"
sed -i "s|@@BUILD_ID@@|$BUILD_ID|" "$TARGET_DIR/init"
chmod 0755 "$TARGET_DIR/init"
echo "$BUILD_ID" > "$TARGET_DIR/etc/taq102-build-id"
echo "post-build: build id $BUILD_ID"
