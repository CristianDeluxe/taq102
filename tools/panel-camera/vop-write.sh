#!/bin/sh
# Write VOP registers and latch them.
#   vop-write.sh 'devmem 0x1010e004 32 0x00000080'
# Nothing written to the VOP takes effect until REG_CFG_DONE is set.
here="$(dirname "$0")"
"$here/tablet.sh" "$1
devmem 0x1010e090 32 0x01" >/dev/null 2>&1
