#!/bin/sh
# Write PHY registers inside the LVDS digital reset.
#   phy-write.sh 'devmem 0x20038390 32 0x80'
# The stock kernel clears E1 bit 7 to stop the LVDS digital block, writes its
# configuration, then sets the bit again. Several fields are sampled only as the
# block leaves reset, so a write made while it runs changes nothing -- which is
# how the E4 common-mode candidate first looked alive and was not.
here="$(dirname "$0")"
"$here/tablet.sh" "devmem 0x20038384 32 0x12
$1
devmem 0x20038384 32 0x92" >/dev/null 2>&1
