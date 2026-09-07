# TODO

> Consolidated from the accessible the working session, the reviewer, Cursor, and Antigravity
> project history. Last reviewed: 2026-09-07. History coverage: Partial.
> Unavailable: the session scratchpads of 2026-09-01 and 2026-09-02 (gone; the
> the reviewer research report they held survives on the archive disk), and the reviewer's
> 2026-09-01 round 3, which never answered. No Cursor or Antigravity history
> touches this project.
>
> States: `[ ]` pending · `[~]` partial or unverified · `[!]` blocked · `[x]`
> verified complete · `[-]` obsolete or superseded. Closed work moves to
> `TODO_LOG.md`.

Device as of 2026-09-05: `boot` = `recovery-taq102-v43-appliance.img` (kernel
v40, PHY PLL at 336 MHz, ramdisk `20260905-002916`), `recovery` = the matching
stock-kernel rescue, BCB zero, proved by a round trip. The journal is
`README.md` here and `~/p/brain/personal/denver-taq102-tablet.md`; images and
checksums are in `/Volumes/Datos4TB2/denver-taq102/`.

## Security

- [ ] The Wi-Fi WPA2 passphrase was typed in clear in a 2026-09-02 session
  transcript (the working session history of `~/p`, around 13:58 UTC) and lives in
  `/data/wifi.conf` on the tablet. Decide whether to rotate the network key or
  accept it (home network, private transcript). Never paste it into this file.
- [ ] `tools/panel-camera/tablet.sh` disables host-key checking against a DHCP
  address. The MAC is pinned in `/data/wifi.mac`, so a DHCP reservation plus a
  `known_hosts` entry is feasible. Accepted trade-off until then.
## Bugs
- [~] `tools/panel-camera/tablet.sh reboot-loader` now detaches the command on
  the tablet and returns at once (edited 2026-09-07, `sh -n` clean); the hang
  itself cannot be re-tested until the next trip into loader mode.

- [ ] Wi-Fi sometimes fails at boot (`sdio_disable_func` -5, then probe -110),
  and a wedged RTL8723CS recovers only by a full power-off; `taq102-wifi`
  retries once through the BSP power nodes, and escalating further in software
  stopped the SDIO card enumerating at all (2026-09-02). Next: count the failure
  rate over cold boots into `/data`, then try cutting the chip's rail through
  the RK816 instead of the BSP nodes. `br2-external/package/taq102-wifi/taq102-wifi`.
- [ ] Rare `[drm] flip_done timed out` followed by two `vop_crtc_enable`, a
  250 ms blackout: 1 in 20 slow flips, 0 in 45 min of glcube (2026-09-04).
  Reproduce with `fliptest` pause mode before touching the driver.
## Kernel and drivers

- [ ] The combo-PHY driver acquires `SRST_MIPIPHY_P` (reset id 36) and never
  pulses it, so the PHY starts from whatever U-Boot left (the reviewer round 2,
  `docs/evidence/2026-09-03-round2-findings.md`). Add a patch that asserts and
  releases it at power-on, then verify the picture and the 336 MHz lock from a
  cold boot.
- [ ] Built-in `CONFIG_PHY_ROCKCHIP_INNO_VIDEO_COMBO_PHY=y` hangs the boot at
  the PHY's first power-on, before any console; loading the module from `/init`
  (`taq102-display`) is the workaround and the cause is open (`README.md`,
  "The display, and what the boot images were really doing wrong"). Patch
  0001's HCLK_VIO_H2P clock did not remove it.
- [ ] The stock-kernel rescue in `recovery` keeps rk816's DC-detect bug: on a
  CDP port it takes 450 mA instead of 1500 mA and drains at brightness 255
  (patch 0004 exists only for our kernel). Accept it, lower the rescue
  backlight, or find the register to poke from user space.
- [ ] GSL3673 driver defects worked around in user space and unpatched in the
  kernel: the suspend path clears tracking ids for slots 1 and up, never slot 0
  (phantom finger; `glcube` expires a silent slot after 0.5 s), and the fb-blank
  notifier holds the chip in reset with no unblank (`taq102-app` unbinds
  fbcon). A driver patch would cover the rescue image and any future app.
- [ ] The device tree names the i2c-2 0x18 sensor `STK8BAxx`; it is a Silan
  SC7A20 (WHO_AM_I 0x11), and the vendor `lis3dh.c` would refuse it too (it
  checks for 0x33), so no DT rename binds a kernel driver. `src/accel.c` drives
  it raw. Drop the node with the next resource-image flash to silence
  `sensor_chip_init: ops is null`; not worth a flash on its own.
- [ ] U-Boot parks the panel-enable GPIO2_B4 low because the hybrid tree has no
  `lvds@20038000` node for it to read; patch 0003 repairs that at the first
  modeset. Adding the node would hand over a lit panel and drop the dependency.
- [ ] Why the stock 4.4.103 does not flicker with the VOP IOMMU while our
  4.4.167 did is unknown and not needed (mainline drops the IOMMU too). Minor.
- [ ] The PLL jitter mechanism is probable, not proven (prediv 12 runs the
  phase detector at 2 MHz, prediv 2 at 12 MHz); a 348 MHz probe on 2026-09-04
  was cut off. Only matters if the panel clock ever changes.

## Appliance

- [ ] Backlight policy: `/init` forces maximum brightness. On a `NONE USB`
  source (450 mA) the tablet drains at 255 and charges at 40; on CDP it charges
  at 255. Decide a fixed default or a charger-aware one (the RK816 input limit
  is readable). `br2-external/board/taq102/rootfs-overlay/init`.
- [ ] A power-off that sticks when unplugged: holding power with USB in reboots
  into charger mode instead of switching off (measured 2026-09-03).
## Infrastructure and tooling

- [ ] Buildroot in the OrbStack VM was OOM-killed twice (exit 137) even at
  `-j1` during the 2026-09-04 the reviewer run; the `taq102` VM holds 16 GB with
  `kbuild` beside it at 5.8 GB. Raise the VM's memory or stop `kbuild` while
  building.
## Pending decisions

- [ ] Mainline track or appliance polish next (asked 2026-09-03, not answered).
  The RK3126 LVDS encoder exists in `jcs/linux-dm250` commit 1d5b0bbb; mainline
  has no RK816 charger driver and `silead.c` lacks the `gsl3673` compatible;
  the UART2 pads below would be wanted.
- [ ] `GLCUBE_FINISH=1` (a `glFinish` between `eglSwapBuffers` and scanout)
  stays a diagnostic; the black flashes were the accelerometer i2c read in the
  render loop, since fixed. Decide after a long run whether to drop the switch.

## Blocked

- [!] Locate the UART2 pads (the stock DT puts fiq-debugger on UART2). Blocked:
  no teardown, pad map or GPL drop exists for the TAQ-102 or the
  BND-RK3126C-D708 sibling. Unblock: open the case and probe near the SoC; the
  headphone-jack theory was reasoned out from the DTB and never measured.
  Needed only for the mainline track.
- [!] Locate the maskrom test point. Blocked on the same teardown. Today's ways
  into loader mode are `reboot-loader` from a running system and both buttons
  from a dark U-Boot, which took several tries each time.

## Future ideas

- [ ] A real application in place of the `glcube` demo; what the appliance is
  for is still unstated.
- [ ] Bluetooth: which of the three RTL8723CS firmware variants this board loads
  (`_cg` and `_vf` are byte-identical, `_xx` differs) is not established, and
  nothing has tried BT.
- [ ] `/dev/rga` (2D accelerator) is available and unused; `particles` could
  blit through it instead of the CPU.
