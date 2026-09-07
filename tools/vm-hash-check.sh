#!/bin/sh
# Compare the tracked sources on the Mac with what the build VM reads through
# the shared mount, before building. `orb cat` truncated a zImage silently on
# 2026-09-03 and the mount served a short glcube.c on 2026-09-04; a build from
# a stale or short file looks exactly like a build from the right one.
#   vm-hash-check.sh [path ...]      defaults to src and br2-external
# Exit 1 and list the files whose hashes differ.
set -eu
repo=$(cd "$(dirname "$0")/.." && pwd)
vm=${TAQ102_VM:-taq102}
cd "$repo"
paths=${*:-"src br2-external"}
# shellcheck disable=SC2086
files=$(git ls-files $paths)
mac=$(printf '%s\n' "$files" | xargs shasum -a 256 | awk '{print $1"  "$2}' | sort -k2)
remote=$(printf '%s\n' "$files" | orb -m "$vm" sh -c "cd '$repo' && xargs sha256sum" | awk '{print $1"  "$2}' | sort -k2)
if [ "$mac" = "$remote" ]; then
	echo "vm-hash-check: $(printf '%s\n' "$files" | wc -l | tr -d ' ') files identical on both sides"
	exit 0
fi
echo "vm-hash-check: MISMATCH (< Mac, > VM)" >&2
a=$(mktemp) && b=$(mktemp)
printf '%s\n' "$mac" > "$a"; printf '%s\n' "$remote" > "$b"
diff "$a" "$b" | grep '^[<>]' >&2 || true
rm -f "$a" "$b"
exit 1
