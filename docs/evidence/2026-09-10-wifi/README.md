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

| `review-briefing.md`, `review-second-opinion.md` | the briefing given to the reviewer (gpt-6-astra) after those three failures, and its answer: an entry-by-entry diff of the vendor's power sequence tables against mainline's, which found the asymmetry that turned out to be the bug |
| `v79-recovers-a-wedged-chip.log` | the fix: v79 booting on a chip the loader-mode reboot had just wedged, and bringing it up anyway. No vendor appliance involved |
| `v79-warm-reboot-1-wifi-up.log`, `v79-warm-reboot-2-wifi-up.log` | two further warm reboots, Wi-Fi associated on both |
| `v79-after-loader-flash-cycle-wifi-up.log` | and a full loader-mode flash cycle, the case the backlog said always landed wedged: Wi-Fi up, 192.168.1.83 |

The first three attempts were reverted. The fix that works is
`kernel/mainline/0018-wifi-rtw88-8703b-complete-the-card-disable-to-card-em.patch`.

## Is it really upstream, or our own bad implementation? (asked 2026-09-11)

A fair challenge, and it is checkable. `trans_carddis_to_cardemu_8703b` is an
upstream table in an upstream file; nothing in this board's series touches
rtw88. The decisive comparison is against the other chips in the same driver:

| table | entries applicable to SDIO |
| --- | --- |
| `trans_carddis_to_cardemu_8723d` | `0x0005` BIT(3)\|BIT(7), SDIO-local `0x0086` write and poll, `0x0005` BIT(3)\|BIT(4), `0x0023` BIT(4) |
| `trans_carddis_to_cardemu_8822b`, `..._8822c` | the same SDIO-local `0x0086` write and poll |
| `trans_carddis_to_cardemu_8703b`, before the patch | `0x0005` BIT(7). One entry. |

8723d is the nearest relative of this chip and its table is, entry for entry
and in the same order, what patch 0018 adds. So this is not a board quirk and
not our implementation: 8703b is the only chip in the driver whose
card-disable-to-card-emulation transition was incomplete, and the patch brings
it in line with its siblings. That argument is now the first paragraph of the
patch's own commit message, because it is the one a maintainer will want.

What our board contributes is only the exposure: on hardware where WL_REG_ON
is the host's only reset and the chip's rail cannot be switched, the asymmetry
has no other way of being undone. A board that can power-cycle the part never
sees it.

## What else the siblings have that 8703b lacks (2026-09-11)

Having found one gap, the obvious next question is whether there are others.
Four comparisons, and only one more turned up something worth changing.

**Upstream history: nothing missed.** No commit in three years touched
`rtw8723d.c`, `rtw8723x.c` or `rtw8723x.h` without also touching `rtw8703b.c`.
The chips have been maintained together, so there is no fix the sibling got and
this one did not.

**The chip description: nothing missing.** `rtw8703b_hw_spec` and
`rtw8723d_hw_spec` share every field name; 8703b in fact carries four the
sibling does not.

**The chip ops: two set to NULL that 8723d implements.** `shutdown` is
`rtw8723d_shutdown`, which sets `BIT_USB_SUS_DIS` in `REG_HCI_OPT_CTRL` and is
USB-only, so it is nothing to this chip. `cck_pd_set` is
`rtw8723d_phy_cck_pd_set`, dynamic CCK packet-detection thresholds: a real
receive-sensitivity feature on 11b in noise, not a defect, and it needs the
chip's own threshold tables rather than a copy. Left alone.

**The power tables, entry by entry.** Two findings:

- `trans_act_to_lps_8703b` writes 0xff to MAC `0x301` on **every** interface.
  That is the PCIe DMA control, and both `trans_act_to_lps_8723d` and Realtek's
  own `Hal8703BPwrSeq.h:139` restrict the same entry to PCI, commented "PCIe
  DMA stop". On an SDIO-only chip it is a pointless poke at a PCIe register on
  every transition into low power. Patch 0019 restricts it.
- `trans_cardemu_to_carddis_8703b` is, entry for entry, the vendor's
  CARDEMU_TO_**PDN** table rather than its CARDEMU_TO_CARDDIS: it sets the
  hardware power-down bit and never asks the SDIO interface to suspend, where
  8723d does both. Recorded and deliberately not changed: with 0018 in place
  the pair is symmetric again, 35 hours of running and repeated reboots say so,
  and rewriting a power-off path with no observed symptom is how a working
  driver stops working.
