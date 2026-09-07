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

## Bugs
- [!] Wi-Fi sometimes fails at boot (`sdio_disable_func` -5, then probe -110),
  and a wedged RTL8723CS recovers only by a full power-off; `taq102-wifi`
  retries once through the BSP power nodes, and escalating further in software
  stopped the SDIO card enumerating at all (2026-09-02). Next: count the failure
  rate over cold boots into `/data`, then try cutting the chip's rail through
  the RK816 instead of the BSP nodes. `br2-external/package/taq102-wifi/taq102-wifi`.
  Blocked: needs repeated cold power-offs of the tablet by hand to measure the rate; the running appliance has been up for days.
- [ ] Rare `[drm] flip_done timed out` followed by two `vop_crtc_enable`, a
  250 ms blackout: 1 in 20 slow flips, 0 in 45 min of glcube (2026-09-04).
  Reproduce with `fliptest` pause mode before touching the driver.
## Kernel and drivers

- [ ] The vendor build warns about DT interrupt properties and executable-stack
  or RWX segments in the link, and the PHY module about macro attributes
  (2026-09-07 build logs). Harmless today; worth a look before a
  warning-clean build is ever wanted.
- [ ] Built-in `CONFIG_PHY_ROCKCHIP_INNO_VIDEO_COMBO_PHY=y` hangs the boot at
  the PHY's first power-on, before any console; loading the module from `/init`
  (`taq102-display`) is the workaround and the cause is open (`README.md`,
  "The display, and what the boot images were really doing wrong"). Patch
  0001's HCLK_VIO_H2P clock did not remove it.
- [ ] The device tree names the i2c-2 0x18 sensor `STK8BAxx`; it is a Silan
  SC7A20 (WHO_AM_I 0x11), and the vendor `lis3dh.c` would refuse it too (it
  checks for 0x33), so no DT rename binds a kernel driver. `src/accel.c` drives
  it raw. Drop the node with the next resource-image flash to silence
  `sensor_chip_init: ops is null`; not worth a flash on its own.
- [!] U-Boot parks the panel-enable GPIO2_B4 low because the hybrid tree has no
  `lvds@20038000` node for it to read; patch 0003 repairs that at the first
  modeset. Adding the node would hand over a lit panel and drop the dependency.
  Blocked: a device-tree change U-Boot itself reads; needs a resource flash and someone at the buttons if it does not come up.
- [ ] Why the stock 4.4.103 does not flicker with the VOP IOMMU while our
  4.4.167 did is unknown and not needed (mainline drops the IOMMU too). Minor.
- [-] Move to a current mainline kernel. Researched 2026-09-07, not worth
  starting: mainline has the SoC (`rockchip,rk3126-vop` and an `arm,mali-400`
  node in `arch/arm/boot/dts/rockchip/rk3128.dtsi`), the Wi-Fi natively
  (`RTW88_8723CS` selects `RTW88_8703B`, so the vendor `8723cs.ko` and its
  vermagic problem disappear) and the GPU through Lima instead of the r7p0
  blob -- but it has no LVDS for this SoC. `rockchip_lvds.c` matches only
  `rockchip,rk3288-lvds` and `rockchip,px30-lvds`, `rk3128.dtsi` has a
  `rockchip,rk3128-dsi-dphy` and no LVDS node, so the combo PHY's LVDS mode
  and the panel path this project spent weeks on would be written from
  scratch. Also missing: the RK816 fuel gauge (the mainline `rk816s` MFD cells
  are pinctrl, clkout, regulator, pwrkey and rtc -- no charger cell, unlike
  RK817), so `/sys/class/power_supply/battery` and everything the control
  centre and the brightness policy read from it would be gone; and the touch,
  whose `gsl3673` config array has no mainline equivalent (`silead.c` knows
  gsl1680/1688/3670/3675/3692). If it is ever wanted, the bounded first step
  is a mainline zImage with the stock `rk3126.dtsi`, no display and no app,
  judged only by whether the ACM console answers.
- [ ] The PLL jitter mechanism is probable, not proven (prediv 12 runs the
  phase detector at 2 MHz, prediv 2 at 12 MHz); a 348 MHz probe on 2026-09-04
  was cut off. Only matters if the panel clock ever changes.

## Appliance

- [!] Hands-on checks on v46 (2026-09-07): open the panel with a swipe down from
  the bar and judge the look; drag brightness; leave it unplugged for the
  chosen minutes and see it sleep, then pick it up and see it wake; tap Wi-Fi
  off and on. Blocked: needs hands and eyes on the tablet.
- [ ] Joining a different Wi-Fi network from the panel needs an on-screen
  keyboard (deferred in the spec); today the network is set in
  `/data/wifi.conf` over ssh.
- [ ] The brightness controller's failed-probe memory and the 30 s cadence
  are tested on the host with traces; a real weak-source run (a plain USB
  port for an hour) has not been recorded yet. Record the level trace from
  the log and check it settles.
- [!] A power-off that sticks when unplugged: holding power with USB in reboots
  into charger mode instead of switching off (measured 2026-09-03).
  Blocked: needs the tablet unplugged and the power button held by hand; RK816 shutdown path in the kernel to read first.
## Infrastructure and tooling

## Pending decisions

- [!] `GLCUBE_FINISH=1` (a `glFinish` between `eglSwapBuffers` and scanout)
  stays a diagnostic; the black flashes were the accelerometer i2c read in the
  render loop, since fixed. Decide after a long run whether to drop the switch.
  Blocked: owner observation over a long run: any black frame with the default build?
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
