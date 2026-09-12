#!/bin/sh
# Fetch cJSON into tools/vendor/cjson/ for the host tests.
#
# The device gets cJSON from Buildroot, which pins 1.7.19; the host tests
# compile the same two files so a parser difference can never be the reason a
# test passes here and fails there. Nothing else in the repository vendors it.
set -eu

VENDOR=$(dirname "$0")/vendor/cjson
CJSON_VERSION=1.7.19
BASE=https://raw.githubusercontent.com/DaveGamble/cJSON/v$CJSON_VERSION

mkdir -p "$VENDOR"
curl -sL "$BASE/cJSON.c" -o "$VENDOR/cJSON.c"
curl -sL "$BASE/cJSON.h" -o "$VENDOR/cJSON.h"

grep -q "CJSON_VERSION_PATCH 19" "$VENDOR/cJSON.h" ||
	{ echo "cJSON.h is not 1.7.19" >&2; exit 1; }
echo "cJSON $CJSON_VERSION ready in $VENDOR"
