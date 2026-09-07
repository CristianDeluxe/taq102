#!/bin/sh
# Run on the Mac. Requires Python 3, NumPy, ffmpeg and the dedicated SSH key.
# Evidence defaults to /tmp/taq102-audit/device/<UTC timestamp>.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
exec python3 -u "$here/test_control_centre_device.py" "$@"
