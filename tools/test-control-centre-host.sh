#!/bin/sh
# Run from the repository root so font fixtures resolve from any caller directory.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out=/tmp/taq102-audit/host-tests
mkdir -p "$out/include/stb"
STB_DIR=${STB_DIR:-/tmp/taq102-audit/stb}
STB_DIR=$(cd "$STB_DIR" && pwd)
ln -sf "$STB_DIR/stb_truetype.h" "$out/include/stb/stb_truetype.h"
CC=${CC:-cc}
cd "$here"
export TEST_OUT="$out"
fail=0
for t in "$here"/tests/control-centre/*_test.c; do
    name=$(basename "$t" .c)
    srcs=$(sed -n 's|^// SOURCES: ||p' "$t")
    set --
    for s in $srcs; do set -- "$@" "$here/src/$s"; done
    if "$CC" -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I "$here/src" -I "$here/tests/control-centre" -I "$out/include" \
        -o "$out/$name" "$t" "$@" -lm && "$out/$name"; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        fail=1
    fi
done
exit "$fail"
