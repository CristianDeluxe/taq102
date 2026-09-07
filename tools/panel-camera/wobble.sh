#!/bin/sh
# Record N seconds of the panel and print per-frame difference to the previous frame.
#   wobble.sh <tag> [seconds]
export LC_NUMERIC=C
tag=$1; secs=${2:-8}
S="${TMPDIR:-/tmp}"
ffmpeg -hide_banner -loglevel error -f avfoundation -framerate 30 -pixel_format uyvy422 -video_size 1280x720 -i "${TAQ102_CAMERA:-0}" -t "$secs" -c:v libx264 -preset ultrafast -qp 0 -y "$S/wobble_$tag.mp4" </dev/null
ffmpeg -hide_banner -loglevel error -i "$S/wobble_$tag.mp4" -vf "select=gte(n\,15),setpts=PTS-STARTPTS,crop=${CROP:-iw:ih:0:0},tblend=all_mode=difference,signalstats,metadata=print:file=-" -f null - 2>/dev/null \
 | awk '/pts_time/{sub(/.*pts_time:/,""); t=$1} /signalstats.YAVG/{sub(/.*=/,""); printf "%.3f %.2f\n", t, $1}' > "$S/wobble_$tag.txt"
awk -v tag="$tag" '{s+=$2; n++; if($2>m){m=$2; tm=$1}} END{printf "%s: frames %d mean %.2f max %.2f at %.2f s\n", tag, n, s/n, m, tm}' "$S/wobble_$tag.txt"
awk 'NR>1{d=$2-prev; if(d>0.5) printf "  spike %.2f s: %.2f (prev %.2f)\n", $1, $2, prev} {prev=$2}' "$S/wobble_$tag.txt" | head -25
