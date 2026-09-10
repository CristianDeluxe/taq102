#!/bin/sh
# Rank the LVDS PHY variants by measured wobble, driving the tablet over the
# USB console instead of ssh (its Wi-Fi is down) and filming with the iPhone.
# The camera must not move between variants: only the differences matter.
set -u
REPO=$HOME/p/taq102
S="$(dirname "$0")"
P=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
[ -n "$P" ] || { echo "no tablet console"; exit 1; }
CROP=${CROP:-1400:100:250:650}
SECS=${SECS:-3}
VARIANTS=${*:-"ours vendor forward pll336 msbsel msbsel-off stock-order source-e4"}

send() { printf '%s\r' "$1" > "$P"; sleep "${2:-1}"; }

stty -f "$P" 115200 raw -echo
pkill -f "cat $P" 2>/dev/null
cat "$P" > "$S/sweep-console.log" 2>&1 &
sleep 1
send '' 1; send 'root' 3

for v in $VARIANTS; do
    send "killall lvdsdiag testpattern 2>/dev/null" 1
    send "(setsid lvdsdiag $v vlines > /tmp/lv.log 2>&1 &)" 4
    timeout 90 ffmpeg -hide_banner -loglevel error -f avfoundation -framerate 30 \
        -pixel_format uyvy422 -video_size 1920x1440 -i "${TAQ102_IPHONE:-0}" \
        -t "$SECS" -c:v libx264 -preset ultrafast -qp 0 -y "$S/sw_$v.mp4" < /dev/null
    python3 "$REPO/tools/panel-camera/zigzag.py" "$S/sw_$v.mp4" 60 "$CROP" \
        | sed "s/^/[$v] /"
done
