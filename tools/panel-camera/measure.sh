#!/bin/sh
# Measure the panel: brightness now, and motion between two frames a second apart.
#   measure.sh <tag>
# With glcube spinning, content on the panel moves and a dead panel does not.
# Calibration, 2026-09-03: backlight off YAVG=33.7, backlight on YAVG=104..118,
# and a static white panel gives motion 0.74..1.06. That is the noise floor:
# anything at it is a dead end. Anything above it is a lead, not a result --
# confirm it with snap.sh and look at the photograph, because the camera's
# auto-exposure alone once produced motion=4.15 on a blank white panel.
here="$(dirname "$0")"
tag="$1"
# macOS mktemp ignores the template when a suffix follows the Xs, so the
# unique part is a directory and the frames live inside it.
dir=$(mktemp -d "${TMPDIR:-/tmp}/panel_${tag}.XXXXXX") || exit 1
a="$dir/a.jpg"
b="$dir/b.jpg"
"$here/snap.sh" "$a"
"$here/snap.sh" "$b"
stat() {
	ffmpeg -hide_banner -loglevel error -i "$1" \
		-vf "signalstats,metadata=print:file=-" -f null - 2>/dev/null \
		| awk -F= -v k="$2" '$1 ~ k {print $2; exit}'
}
motion=$(ffmpeg -hide_banner -loglevel error -i "$a" -i "$b" \
	-filter_complex "[0:v][1:v]blend=all_mode=difference,signalstats,metadata=print:file=-" \
	-f null - 2>/dev/null | awk -F= '/YAVG/{print $2; exit}')
printf "%s: YAVG=%s YLOW=%s YHIGH=%s motion=%s\n" \
	"$tag" "$(stat "$a" YAVG)" "$(stat "$a" YLOW)" "$(stat "$a" YHIGH)" "$motion"
