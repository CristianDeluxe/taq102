#!/bin/sh
# Record the panel through the iPhone for each PHY variant and print the
# measured horizontal wobble, so the variants are ranked by a number rather
# than by an impression. The camera must be still: a hand-held phone puts its
# own tremor into the measurement through the rolling shutter.
#   sweep.sh [seconds] [variant ...]
set -u
here="$(dirname "$0")"; S="${TMPDIR:-/tmp}"
SECS=${1:-4}; shift 2>/dev/null || true
VARIANTS=${*:-"ours forward msbsel-off source-e4 stock-order"}
ssh_t() { ssh -i "$HOME/.ssh/taq102" -o ConnectTimeout=3 -o StrictHostKeyChecking=no \
              -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@${TAQ102_HOST:-192.168.1.57} "$@"; }
for v in $VARIANTS; do
    ssh_t "killall lvdsdiag 2>/dev/null; sleep 1; (setsid /usr/bin/lvdsdiag $v vlines > /tmp/lvdsdiag.log 2>&1 &); sleep 3; grep -c holding /tmp/lvdsdiag.log" > /dev/null
    ffmpeg -hide_banner -loglevel error -f avfoundation -framerate 30 -pixel_format uyvy422 \
        -video_size 1920x1440 -i "${TAQ102_IPHONE:-2}" -t "$SECS" -c:v libx264 -preset ultrafast -qp 0 \
        -y "$S/sweep_$v.mp4" < /dev/null
    python3 "$here/zigzag.py" "$S/sweep_$v.mp4" 60 1500:300:200:1110 | sed "s/^/[$v] /"
done
