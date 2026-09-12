#!/bin/sh
# Host tests for the DMX desk. Run from anywhere; paths resolve from the repo.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out=${TEST_OUT:-/tmp/taq102-audit/dmx-desk}
mkdir -p "$out"
CC=${CC:-cc}
cd "$here"
fail=0
for t in "$here"/tests/dmx-desk/*_test.c; do
    name=$(basename "$t" .c)
    srcs=$(sed -n 's|^// SOURCES: ||p' "$t")
    set --
    for s in $srcs; do set -- "$@" "$here/src/$s"; done
    if "$CC" -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I "$here/src" -I "$here/tests/dmx-desk" \
        -o "$out/$name" "$t" "$@" -lm && "$out/$name"; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        fail=1
    fi
done
exit "$fail"
