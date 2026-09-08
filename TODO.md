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
- [ ] The charging bolt is a 5-by-7 bitmap scaled up, so it is the one blocky
  shape left in a bar that is otherwise smooth. Only visible while charging
  and only at close range; a small vector path would settle it.
- [ ] The -256 dBm gate in `read_wifi` is untested against a live
  unassociated interface: it went in after the link had already been
  recovered. Check it the next time the chip comes up without associating.
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
- [~] Move to a current mainline kernel. 2026-09-08: the kernel side is
  written and builds against 7.3.0-rc2 -- zImage 13234688 bytes, no warnings,
  nine patches in `kernel/mainline/` with the apply order and the evidence in
  its README. LVDS, the RK816 fuel gauge, the GSL3673 touch through mainline's
  own `silead.c`, the eMMC and SDIO, and a config fragment whose 32 symbols
  were each checked against the post-`olddefconfig` `.config`. The PHY needed
  no PLL patch: mainline's LVDS path hardcodes the same 336 MHz divider pair
  `patches/0001` forces. Nothing has run on hardware.
  Blocked on userspace, not on the kernel: `glcube` links the r7p0 Utgard blob,
  which cannot talk to `CONFIG_DRM_LIMA`, so this kernel would draw nothing.
  Next: a Buildroot branch with Mesa's lima driver and glcube relinked against
  it, plus `silead/gsl3673.fw` and `rtw88/rtw8703b_fw.bin` in the initramfs --
  both are requested asynchronously, so missing they give a silently absent
  touchscreen and a silently absent wlan0.
  2026-09-08: the userspace exists now -- `taq102_mainline_defconfig` builds it
  with Mesa/Lima instead of the Utgard blob, and both firmware blobs are in the
  image. What blocks the port is no longer userspace but the kernel not
  reaching it; see `kernel/mainline/README.md`, "Four boots, no output".
- [x] Recovered 2026-09-08: the button dance landed, the BCB was zeroed and the
  tablet is back on v49 with glcube running. The crash log was NOT recovered --
  see the next item.
- [ ] The next mainline attempt must not lose its crash log again. Two causes,
  both now fixed but neither yet exercised: `tools/recover-from-mainline.sh`
  used the default known_hosts, where an older key for 192.168.1.51 fails
  verification, so its ssh dump never ran; and 0x68100000 is ordinary RAM to
  the vendor kernel, which had already put modetest and libevdev there by the
  time it was read three minutes in. The rootfs `/init` now copies that region
  to /tmp before /data is even mounted, and parks it under /data afterwards.
- [x] **Mainline boots.** 2026-09-08, v59: Linux 7.3.0-rc2 runs on the tablet
  with the RK816 battery driver reporting real values, eMMC and /data, rtw88
  associated with a DHCP lease, and the USB ACM console working. Evidence:
  `docs/evidence/2026-09-08-mainline/`.
- [x] **The panel works on mainline.** 2026-09-08: the kernel console is
  visible on the tablet's own screen at the panel's real 1024x600, the LVDS
  connector reports connected with the right 125x223 mm physical size, and the
  VOP, our LVDS encoder patch and the PHY are all proved on hardware. It took
  finding two mainline bugs; both are written up in
  `kernel/mainline/ISOLATING-THE-DISPLAY-HANG.md` with before/after evidence.
- [ ] Replace the `fw_devlink=off` workaround with the narrow fix. That boot
  argument disables device links machine-wide; the real change is
  `GENPD_FLAG_NO_SYNC_STATE` on the Rockchip power domains, which today set
  only `GENPD_FLAG_PM_CLK | GENPD_FLAG_NO_STAY_ON`. One line, and it needs the
  same A/B the workaround got.
- [ ] Send both patches upstream: the genpd deadlock (with the stack trace in
  `docs/evidence/2026-09-08-mainline/genpd-deadlock-stack.txt`) and
  `0010-drm-rockchip-lvds-do-not-hijack-the-panel-bridge.patch`. Neither is
  board-specific.
- [ ] glcube still has no GPU: lima needs `gpu-sched`, which was left out of
  the initramfs (`lima: Unknown symbol drm_sched_init`). Packaging, not a
  defect. Also `modetest` fails to create a dumb buffer with -EINVAL, which is
  worth understanding before blaming Mesa for anything.
- [ ] Touch is untested: `silead` did not appear in the boot log, only the
  rk805 pwrkey. Check whether the controller probed at all.
- [-] Flash `recovery-taq102-v55-mainline-beacon.img` and read the panel. Built
  and verified 2026-09-08: same kernel and resource image as v54, ramdisk
  carrying the backlight beacon, the ramoops rescue and the userspace marker.
  Unlike v50-v54 this one reports something whatever happens -- count the
  flashes (1 no UDC, 2 UDC unbound, 3 bound but unconfigured, 4 configured),
  and if it dies earlier, v49's own /init rescues the log on the way back.
  Costs I one button dance; arm `tools/recover-from-mainline.sh` first.
- [-] **Owner's task: recover the tablet.** It currently boots the mainline
  diagnostic image (v54) from `recovery` and stops with a white screen,
  enumerating nothing on USB, so there is no software way back in. The BCB
  still says `boot-recovery`, which is why every power-on returns to it.
  The sequence, from I, 2026-09-08: unplug USB, hold power ~10 s until
  the panel goes dark, then hold both buttons and plug USB in while holding,
  keeping them down 10-15 s. A white screen means that pass already failed.
  Measured unreliable on this board and it took several tries once before.
  When it lands, two waiters do the rest automatically: one zeroes the BCB at
  LBA 24608 (`scratchpad/rescue-bcb.sh`) and `boot` = v49 comes back; the other
  dumps ramoops at 0x68100000 over ssh (`scratchpad/grab-ramoops.sh`). Re-arm
  them before the attempt -- they expire on a deadline.
- [ ] Read the mainline crash log out of ramoops at 0x68100000 and act on it.
  It is a race: that address is ordinary RAM to the vendor kernel, so the dump
  must happen in the first seconds after recovery. The log should name the
  driver that failed, which is now believed to be DWC2 or the Rockchip USB2
  PHY, since the white screen proves the kernel reached the driver phase.
- [!] Flashing a mainline kernel is a one-way door and is not authorised yet.
  The SD card is not an escape: Rockchip's BootROM tries eMMC before SD, so a
  card is never reached while `boot` holds a valid loader, which is why the
  2026-09-02 SD rescue was abandoned. A kernel that does not come up also has
  no USB console, because the ACM gadget is raised by our `/init`. The way back
  is loader mode on both buttons, measured unreliable but proved once from the
  dark U-Boot of the truncated v29. Decide with I, with hands on the
  tablet, and only once the userspace above exists.
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
