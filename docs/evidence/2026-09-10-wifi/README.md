# Evidence, 2026-09-10: the RTL8723CS warm-reboot wedge

The account this supports is in the repository `README.md`, section "The Wi-Fi
wedge is not a rail, and mainline cannot undo it". Every log below is the
tablet's USB console (`ttyGS0`), ANSI escapes stripped.

| file | what it shows |
| --- | --- |
| `rail-and-gpio-experiments-v76.log` | the wedged chip under v76: gpio2 PB5 held low with the level read back on `EXT_PORTA`, PB1 already low, RK816 LDO4/LDO5/LDO6 cut singly and all three together with the enable registers read back, each followed by an unbind and bind of `10218000.mmc` that re-enumerates the card and fails the same poll |
| `cold-boot-still-wedged-v76.log` | the boot after my full power-off with USB unplugged: still `mac power on failed`, so removing power does not clear it |
| `v59-reflashed-fails-identically.log` | v59, the image whose 2026-09-08 boot is the one in the evidence with mainline Wi-Fi working, reflashed and booted: same failure. This is what rules out a kernel regression |
| `v59-clock-cut-and-bcb-inspection.log` | the RK816 32.768 kHz clkout2 (register 0x20) turned off for three seconds with PB5 held low, then restored: no change. Also the read of `/dev/mmcblk1` showing the tablet's sector numbering does not match the flasher's LBAs |
| `vendor-appliance-wlan0-up.log` | the v49 vendor appliance on the same chip: `wlan0` at 5 s, associated, mdnsd announcing. The chip is not broken |
| `v76-boot1-after-vendor-wifi-up.log` | mainline immediately after the appliance had cleaned the chip: authenticated, associated, addressed |
| `v76-boot2-warm-reboot-wedged.log` | one `reboot` later, wedged again. Powering the MAC off in `rtw_sdio_shutdown` did not prevent it |
| `v77-forced-carddisable-retry-fails.log` | forcing `chip->pwr_off_seq` and retrying in `rtw_mac_power_on`: the chip fails the disable sequence too (`failed to poll offset=0x5f8`), then power-on again |
| `v78-boot1-after-vendor-wifi-up.log`, `v78-boot2-pwrseq-delay-wedged.log` | `post-power-on-delay-ms = <200>` on the pwrseq, the value PinePhone uses for this part: works from a clean chip, wedges on the next reboot like the others |
| `v75-restored-wifi-up.log` | the tablet as it was left: v75, Wi-Fi associated, 192.168.1.79 |

All three driver and device-tree changes were reverted; the tree carries none
of them.
