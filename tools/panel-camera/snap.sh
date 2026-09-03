#!/bin/sh
# Grab one webcam frame of the panel.
#   snap.sh shot.jpg
# The first frames of an avfoundation capture are pure black while the sensor
# warms up, so 25 of them are discarded before the one that is kept.
ffmpeg -hide_banner -loglevel error \
	-f avfoundation -framerate 30 -video_size 1280x720 \
	-i "${TAQ102_CAMERA:-0}" -vf "select=gte(n\,25)" \
	-frames:v 1 -update 1 -y "$1" >/dev/null 2>&1
