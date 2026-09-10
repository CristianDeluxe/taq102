# TODO

> Consolidated from the accessible the working session, the reviewer, Cursor, and Antigravity
> project history. Last reviewed: 2026-09-09. History coverage: Partial.
> Unavailable: the session scratchpads of 2026-09-01 and 2026-09-02 (gone; the
> the reviewer research report they held survives on the archive disk), and the reviewer's
> 2026-09-01 round 3, which never answered. No Cursor or Antigravity history
> touches this project.
>
> States: `[ ]` pending · `[~]` partial or unverified · `[!]` blocked · `[x]`
> verified complete · `[-]` obsolete or superseded. Closed work moves to
> `TODO_LOG.md`.

Device as of 2026-09-09: `boot` = `recovery-taq102-v43-appliance.img` (kernel
v40, vendor 4.4.167), `recovery` = `recovery-taq102-v75-cube-touch-x.img` (mainline
7.3.0-rc2, the 17-patch series, touch geometry measured, glcube scaled), BCB = `boot-recovery`, so every
power-on runs the mainline cube. `tools/loader-watch.sh bcb` puts the vendor
appliance back. The journal is `README.md` here and
`~/p/brain/personal/denver-taq102-tablet.md`; images and checksums are in
`/Volumes/Datos4TB2/denver-taq102/`.

## Security

## Bugs
- [-] Tembleques on the panel, reported 2026-09-10 on v79 and gone by the end
  of the same session without any display change. Probably self-inflicted, and
  worth reading before poking the PMIC with the panel live: the report came
  minutes after RK816 LDO4, LDO5 and LDO6 were disabled for half a second each
  and then all three together for three seconds, hunting the Wi-Fi wedge. LDO6
  is the rail whose absence once left this panel unpowered. The tablet was
  rebooted several times afterwards and the artifact went with them. Not proven,
  but the timing is close and the alternative is an artifact no instrument could
  find. Measured while it was reportedly present, and all of it clean:
  displacement 0.008 panel px against a 0.006 floor while the pre-fix PHY
  configuration still measured 0.411 on the same rig; brightness flat across
  vertical lines, grey, white and amber; page flips every vblank, every 100 ms
  and not at all indistinguishable; backlight 255 down to 24 with no PWM
  signature. Tables in `docs/evidence/2026-09-10-panel/`.
  Left behind by the hunt, and worth keeping: `fliptest` is in the mainline
  image's `taq102-diag` package now, `tools/panel-camera/flicker.py` measures
  brightness the way `zigzag.py` measures displacement, and the iPhone works as
  a Continuity Camera over USB-C on the Mac mini.
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
- [ ] The next mainline attempt must not lose its crash log again. Two causes,
  both now fixed but neither yet exercised: `tools/recover-from-mainline.sh`
  used the default known_hosts, where an older key for 192.168.1.51 fails
  verification, so its ssh dump never ran; and 0x68100000 is ordinary RAM to
  the vendor kernel, which had already put modetest and libevdev there by the
  time it was read three minutes in. The rootfs `/init` now copies that region
  to /tmp before /data is even mounted, and parks it under /data afterwards.
- [ ] Replace the `fw_devlink=off` workaround with a real fix. The one-line
  candidate, `GENPD_FLAG_NO_SYNC_STATE` on the Rockchip power domains, was
  tried in v63 with the display stack built in and the boot still hangs before
  userspace (2026-09-09), so the mechanism is not fully understood. Needs a
  trace from a built-in boot: an early printk of the blocked task onto the
  bootloader framebuffer is the channel that would survive it.
- [ ] Package the mainline modules. v66's ramdisk is hand-assembled:
  `gpu-sched.ko`, `lima.ko`, `drm_shmem_helper.ko` and
  `phy-rockchip-inno-dsidphy.ko` copied from the kernel build into the
  Buildroot rootfs next to `taq102-cube` (now `br2-external/package/taq102-cube`).
  The mainline defconfig should take them from the kernel's `modules_install`,
  and `taq102_mainline_defconfig` should build the kernel from
  `kernel/mainline/` in the first place.
- [ ] Describe the RK816 regulators in the mainline board DTS, hand lima
  `vdd_logic` as `mali-supply`, and only then put `operating-points-v2` back
  on `&gpu`. v66 ran devfreq over the OPP table with the clock alone and the
  Mali hung at 480 MHz after 73 s, unrecoverable by lima's reset (2026-09-09);
  v67 deletes the table and runs at the bootloader's 148.5 MHz, which draws
  the cube at the panel's rate. Vendor figures for the pairing are in
  `kernel/rk3126-taq102-hybrid.dts` (gpu opp table, phandle 0x19).
- [ ] `modetest` cannot create a dumb buffer (-EINVAL). Moot for glcube,
  which allocates through GBM and lima, but still unexplained.
- [ ] Send upstream. The series was reviewed and rebuilt as `git am`-able
  patches on 2026-09-09 (`kernel/mainline/FINDINGS.md`, "The series,
  reviewed"): 0011 (LVDS panel-bridge fix) and 0012 (silead NAK quirk) are
  ready to post; 0001-0004 pass `dt_binding_check` and `dtbs_check`, which
  now run in the VM. Before posting the board: the panel's part number for
  `panel-lvds`. Tried without opening the case (2026-09-09): no name in
  the vendor DTB, U-Boot, kernel, vendor or system partitions (`strings`,
  the only panel names there are panel-simple's own table); the measured
  timing (51.2 MHz, 160/160/23/12) is the generic 1344x645 convention and
  matches two 7" SPWG panels in panel-simple, not ours (10.1", JEIDA-24,
  125x223 mm); no 1024x600 JEIDA panel exists in panel-simple; web listings
  of replacement screens for the TAQ-101xx/102xx family are behind 403s
  for a script but may show the label in their photos. Otherwise the case. MAINTAINERS is done: the charger
  has an entry in 0008 and the DTS is covered by the Rockchip glob. The genpd
  deadlock goes as a bug report with
  `docs/evidence/2026-09-08-mainline/genpd-deadlock-stack.txt`.
- [x] Wi-Fi on mainline after a warm reboot: fixed 2026-09-10 by patch 0018,
  `wifi: rtw88: 8703b: complete the card-disable to card-emulation transition`.
  `trans_cardemu_to_carddis_8703b` sleeps the chip's 12H LDO (`0x23[4]`) and
  suspends the SDIO interface; `trans_carddis_to_cardemu_8703b` only cleared the
  power-down bit, so neither was ever undone and the WLAN MAC stayed unpowered
  while the card still enumerated. v79 recovers a chip wedged by a loader-mode
  reboot on its first boot and survives repeated warm reboots. Evidence in
  `docs/evidence/2026-09-10-wifi/`, account in `README.md`.
- [~] Touch on mainline: the GSL3673 NAKs the data byte of the reset write
  (`0xe0 = 0x88`) while applying it, and `silead.c` treated that as a probe
  failure. Patch 0011 accepts the NAK; v68 probes (`input0 = silead_ts`,
  firmware loaded in 9 s) and glcube opens the device. Not yet seen: a touch
  event. The interrupt line (gpio2 15, rising, as the vendor) had fired zero
  times before I had touched the panel. Owner to touch; if the count
  stays at zero with a finger on the glass, it is the INT line or its mux.
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

- [ ] Mac side: Pioneer DJ's `FwUpdateManagerd` (LaunchDaemon
  `com.pioneerdj.FwUpdateManagerd`) can wedge `IOServiceOpen` for every
  libusb client at boot; `rkdeveloptool ld` then hangs on its first device
  and `loader-watch.sh` polls forever. Killing the daemon frees it
  (2026-09-09). Either unload the daemon or make `loader-watch.sh` run
  `rkdeveloptool` under a per-call timeout and say so.
- [ ] Mainline `u_serial` warns in `gs_close()` when the host vanishes with
  the ACM console open (seen when the Mac rebooted). Harmless; a report for
  the gadget list some day.

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

- [~] Soak the mainline cube: v67 was healthy at 20 min on 2026-09-09 (see
  TODO_LOG); the vendor appliance has run for days. Leave v67 running
  unplugged and plugged and note the first freeze, if any, with `dmesg`.
- [ ] A real application in place of the `glcube` demo; what the appliance is
  for is still unstated.
- [ ] Bluetooth: which of the three RTL8723CS firmware variants this board loads
  (`_cg` and `_vf` are byte-identical, `_xx` differs) is not established, and
  nothing has tried BT.
- [ ] `/dev/rga` (2D accelerator) is available and unused; `particles` could
  blit through it instead of the CPU.
