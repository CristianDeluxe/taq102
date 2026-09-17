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
# actions_test duplicates a descriptor onto 512 to prove the worker closes what
# it does not need. A non-interactive shell can start at 256, where that dup
# fails and the test aborts for a reason that has nothing to do with the code.
ulimit -n 1024 2>/dev/null || true
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
        -o "$out/$name" "$t" "$@" -lm -pthread && "$out/$name"; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        fail=1
    fi
done
exit "$fail"
