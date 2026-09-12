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
- [ ] `usb2phy` charger detection reports nothing: `/sys/class/extcon/extcon0`
  shows `USB`, `SDP`, `CDP`, `DCP` and `SLOW-CHARGER` all zero with VBUS
  present and the gadget enumerated (2026-09-10, v83). The rk3128 config in
  `phy-rockchip-inno-usb2.c` does carry a `chg_det` block, so this is a wiring
  or a bvalid problem rather than an unimplemented feature. Until it works the
  RK816 charger cannot tell a wall adapter from a laptop port, and the board's
  declared `input-current-limit-microamp` is what stands in for it. Evidence:
  `docs/evidence/2026-09-10-charging/`.
- [!] Tembleques on the panel, third sighting 2026-09-12 (observed: "es la
  tercera vez"), 17 h into the boot, on the desk and then on the cube, at any
  backlight level. The 2026-09-10 verdict of "probably self-inflicted by the
  LDO hunt" is withdrawn: nothing touched the PMIC this time. What this
  sighting established, in order: the PHY PLL was at the good pair
  (`REG03=0x02 REG04=0x1C`, 336 MHz) and the LVDS block at `E1=0x92 E4=0xAA
  EB=0xF8` while I was seeing it; LDO4/5/6 enabled at 3.3 V; no
  kernel event; the iPhone rig again measured the floor (0.004 px at 30 fps,
  0.025 at 60 fps, flicker 0.13 levels). Then, by my eye, with the
  fault still believed present: `testpattern bars` clean, `rescue-screen`
  (Inter text, one buffer) clean, the desk static (1 flip) clean, the desk
  flipping identical buffers at 49/s clean, the cube clean again. So it had
  cleared itself between the report and the first pattern, as on the 10th,
  and every A/B after that ran in the clean state and proves nothing about
  the mechanism. Blocked on the next occurrence. Unblock: `panel-trap` now
  logs the PHY, VOP clocks, rails, charger, load, drawing app and kernel
  lines once a minute to `/data/panel-trap.log` (in `taq102-diag`, started by
  hand on 2026-09-12; make the image start it). When it shimmers again: note
  the time, do not restart anything, `diff` that minute against a clean one,
  and run the camera on `fliptest vlines` before it clears.
- [ ] Three of the tablet's USB gadget resets on 2026-09-12 (`dwc2: new
  device` at uptime 51602, 64162, ~65830) lined up with three QLC+ launches on
  the Mac, whose DMX USB plugin walks every USB device with libusb. A fourth
  launch at 66900, watched on purpose, reset nothing. So it is a correlation
  from three points, not a mechanism; keep it in mind, do not build on it.
- [!] The tablet drains on the USB-C hub with the input limit at 1500 mA,
  and a write to the RK816's USB_CTRL register un-sticks it. 2026-09-12,
  brightness 255, `0xa1=0x45` (VLIM 4.4 V, ILIM index 5) throughout: from
  before 10:24 until 11:00 local `current_now=-259..-292 mA`; 11:00 to 11:07
  `+300..+336 mA`; 11:07 on `-231..-260 mA` again, with VBUS (RK816 USB ADC
  0xC0/C1, scaled against the battery ADC) sitting at **4.44 V**, which is
  the input voltage limiter's threshold. `i2cset 0xa1 0x05` (VLIM 4.0 V) gave
  `+666 mA` and VBUS 3.97 V within 15 s; restoring `0x45` did **not** bring
  the throttle back: `+650 mA` at VBUS 3.98 V for the next ten minutes and
  counting. So the limiter latches into a throttled state on some VBUS event
  and stays there until the register is rewritten, and the 11:00 recovery
  was probably such a rewrite (something re-ran `update_cables`). The source
  itself is soft, about 0.5 ohm from the swing (0.9 A for 0.46 V), which is
  the cable or the hub port, and is why the threshold is reached at all.
  Not yet known: whether a same-value rewrite re-arms it, or only a change;
  what the VBUS event is (another device on the hub drawing, a USB reset).
  Unblock: `panel-trap` now logs VBUS and `0xa1` every minute; at the next
  drain, first try `i2cset -f -y 2 0x1a 0xa1 0x45` (same value) and read the
  current 15 s later. If that re-arms it, the fix is a periodic rewrite in
  `rk816_charging_monitor` when plugged in and discharging; if only a change
  does, the fix toggles VLIM. Either way it is a driver change we own.
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

- [~] Patches 0018 and 0019 are with upstream: delivered 2026-09-11 to Ping-Ke
  Shih, linux-wireless and linux-kernel, from `me@cristiandeluxe.dev` via nova,
  all three messages `Completed` with an empty queue. The thread should be in
  `lore.kernel.org/linux-wireless`; review mail goes to that account. Nothing
  to do until someone replies.
- [ ] `trans_cardemu_to_carddis_8703b` is the vendor's CARDEMU_TO_PDN table
  under the card-disable name: it sets the hardware power-down bit and never
  asks the SDIO interface to suspend, where `trans_cardemu_to_carddis_8723d`
  does both (compared 2026-09-11). Harmless since patch 0018 made the reverse
  transition undo everything the PDN path sets, and 35 hours of running say so,
  but it is the same family of defect as 0018 and worth reporting once someone
  can test a power-off path change.
- [ ] `cck_pd_set` is NULL for 8703b where 8723d has
  `rtw8723d_phy_cck_pd_set`: dynamic CCK packet-detection thresholds, which
  help 11b reception in noise. A feature this chip could have rather than a
  defect; it needs 8703b's own threshold tables, not a copy of the sibling's.
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
- [~] Touch on mainline: the controller is not scanning, which is a different
  fault from the one this item used to describe. Measured 2026-09-10 on v85
  while I was asked to touch the glass: the interrupt on gpio2 15
  stayed at 164, its count from probe, across three windows totalling a minute;
  the pin's own level read 0 for 40 consecutive samples over 20 s (`devmem
  0x20084050` bit 15), with the iomux confirmed as GPIO (`0x200080cc` reads 0);
  and the controller itself reported **zero fingers over i2c** the whole time
  (`i2cget -f -y 2 0x40 0x80`). So it is not the INT line or its mux, which is
  where this item pointed before: the GSL3673 is not detecting anything to
  report. Probe succeeds and the firmware loads, so the suspicion is that the
  reset write whose data byte NAKs (patch 0012 accepts the NAK) leaves the chip
  loaded but never started. Unconfirmed until I says they touched the
  glass inside one of those windows.
  Next: compare the vendor driver's post-firmware start-up writes against
  `silead.c`, the same table-by-table way the Wi-Fi wedge was found; the vendor
  source is in the VM at
  `/work/kernel/drivers/input/touchscreen/gslX680*`.
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
- [-] Flashing a mainline kernel is a one-way door and is not authorised yet.
  Overtaken by events: mainline images have been flashed to `recovery` and
  booted repeatedly since 2026-09-08, and `/usr/sbin/reboot-loader` plus the
  PMU poke (`devmem 0x100a0038 32 0x5242C301`) make loader mode reachable from
  software, so the door is no longer one-way. `boot` still holds the vendor
  appliance and the BCB still selects between them.
- [ ] The PLL jitter mechanism is probable, not proven (prediv 12 runs the
  phase detector at 2 MHz, prediv 2 at 12 MHz); a 348 MHz probe on 2026-09-04
  was cut off. Only matters if the panel clock ever changes.

## Appliance

- [~] Sleep and the boot logo, v87 (2026-09-11). Auto-sleep now runs on the
  charger too, and the pick-up detector arms on stillness rather than on a
  two-second stopwatch, so the tablet no longer wakes itself moments after the
  power key. Measured: slept by itself while plugged, still asleep four minutes
  later, zero wake events. The boot screen is now the Vibra wordmark white on
  black instead of the vendor's white logo.
  Not yet confirmed by hand: that the power key still *wakes* it. Needs the
  owner.
- [!] The new boot logo draws as a smudge (observed, 2026-09-11, v87). The BMP is
  provably well formed -- 600 rows of exactly 1024 pixels, runs only, the
  vendor's own palette since v88 -- and Pillow decodes it back correctly, so
  the fault is in what U-Boot makes of it rather than in the file's shape.
  Blocked on seeing it: `uboot_logo=0x02000000@0x9dc00000` is a reserved
  simple-framebuffer that `/dev/mem` refuses under `CONFIG_STRICT_DEVMEM` and
  that DRM has replaced by the time there is a shell, so the boot has to be
  filmed. The iPhone works as a Continuity Camera over USB-C on the Mac mini.
  Cheapest discriminator once there are eyes: flash a resource image with the
  stock logo and see whether DENVER appears. If it does, U-Boot draws our
  resource and the file is at fault; if the screen is white, U-Boot never drew
  our logo and the smudge is something else entirely.
- [ ] Real suspend-to-RAM is still unexplored. Today's sleep keeps the SoC
  running, so it saves the backlight's ~300 mA and nothing else. I
  asked for the current behaviour to be fixed first and for suspend to be
  investigated afterwards; whether RK3126 suspend works at all on mainline is
  unknown, and a failed attempt can hang the boot.
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

## DMX desk (Phase A)

The tablet as a control surface for the Vibra QLC+ show, with the show
MacBook as master. Design and evidence:
`docs/2026-09-12-dmx-desk-design.md`, the reviewer review in
`docs/evidence/2026-09-12-dmx-desk-findings.md`.

- [!] Fog cannot be a held button from the tablet. The show's `HUMO` cue is
  VC widget 125, `actionType: 1` (Flash), function 364: on while held, off on
  release. If the link drops between press and release the machine stays on
  and the tablet cannot stop it, and QLC+ 5.2.2 has no master-side timeout
  (checked in the 5.2.2 sources). Unblock: add a finite fog cue (single-step
  chaser with a duration) to the prepared workspace and bind the tablet to
  that. Until then the desk refuses to load a map with a `momentary` fog
  control.
- [ ] Experiment 1, the protocol against a prepared show: on the 5.2.2 Mac
  with the DMX interface unplugged, drive widgets 108, 35, 28, the grand
  master, speed dial 54, XY pad 0, a new colour slider and the fog cue from a
  WebSocket probe. Confirm one message per Toggle gesture, solo-frame
  behaviour when Mac and tablet alternate, the coarse/fine values the XY pad
  produces once its `hMin=0, hMax=0` range is fixed, colour takeover and
  release, and resynchronisation after a reconnect. Then kill the client
  between a flash press and its release and watch the fog stay on.
- [ ] Experiment 2, the link cold with no venue Internet: Mac as AP from a
  cold start (band, security, DHCP, firewall, reachability), repeated after a
  reboot, then the same on the travel router. Thirty minutes of timestamped
  traffic while dragging two controls, reporting p50/p95/p99 of the
  application round trip. Then pull the Wi-Fi: controls must disable within
  750 ms, nothing may replay, the Mac must still run the show.
- [ ] Experiment 3, the panel under a desk's load: throwaway benchmark with
  the existing painter (XY pad, cached colour field, two faders, warmed text,
  status updates, live network traffic) recording p50/p95/p99 paint and
  present, missed flips, input-to-queued-command, RSS, `MemAvailable`, packed
  image size and battery current. A fresh touch drag test comes first, since
  the v75 and v85 notes disagree about the touch stack.
- [ ] Prepare the gig copy of the workspace: save `DeluxeEventos.qxw` as a
  tablet copy in 5.2.2, fix the XY pad horizontal range, add the Click-and-Go
  colour sliders the desk needs, add the finite fog cue, and publish the show
  map against that copy's hash. The original stays untouched.
- [ ] `DMX-Fixtures` has no `docs/` in its tip, although the brain page
  describes five documents there (`qxw-format.md`, `rig.md`,
  `show-operation.md`, `qlcplus-environment.md`, `toolkit.md`). Find out
  whether that work was ever committed, and fix either the repo or the page.

- [ ] The XY pad's range disagrees between the two sources: the workspace's
  `<Pan Min="0" Max="255"/>` and `<Tilt Min="0" Max="255"/>` against
  `/vc.json`'s `horizontalRange {min:0,max:0}` and `verticalRange
  {min:0,max:256}` for the same widget 0 (2026-09-12). One of them is not the
  window the engine clamps to. Settle it in experiment 1 by sending the
  extremes and reading the resulting DMX, before the desk maps a finger onto
  either.
- [ ] Blackout has no route yet. QLC+'s own Blackout button toggles the global
  blackout on every request and then shows whatever state the sender claimed,
  so its widget state cannot stand for the rig's, and grand master at zero is
  not blackout. The tile ships disabled and showing unknown until experiment 1
  settles a route with an authoritative readback.
- [ ] The desk dropped its link once in 20 minutes of idle running on 2026-09-12
  (`803 ms without a word from the master`, one line in `/tmp/dmxdesk.log`,
  reconnected by itself). The 750 ms staleness threshold may simply be tighter
  than this Wi-Fi, which already has its own `rtw88_8723cs: failed to get tx
  report` history. Measure the heartbeat round trip over an hour before
  loosening it: a threshold chosen to hide a stall is worse than a drop that
  recovers.
- [x] The desk needs its own heartbeat. Pushes are change-driven and the
  server's WebSocket ping is every 5000 ms, so the 750 ms staleness rule would
  disconnect a healthy idle desk. Acceptance: a scene held untouched for ten
  minutes with the link still up.
- [ ] Validation needs a manifest generated on the Mac from the prepared
  `.qxw` and the fixture definitions: `/vc.json` has no speed-group
  membership, no fixture or channel bindings, no solo-frame exclusivity and no
  fog dependency graph, and a different workspace can keep every id while
  changing what the scenes output.
- [ ] Only 24 of this show's 376 functions stop on their own
  (`tools/show-manifest.py`). That is the general case behind the fog rule:
  most cues run until something stops them, so any control the desk fires has
  to have a stopping story, not just a starting one.
- [~] The desk as a professional busking surface: the plan is
  `docs/2026-09-12-desk-pro-design.md` (the reviewer reviews in
  `docs/evidence/2026-09-12-desk-pro-findings.md` and
  `docs/evidence/2026-09-12-desk-vibra-findings.md`). Approved 2026-09-12.
  The show is `Vibra.qxw` on DMX-Fixtures' `qlctool` branch (worktree
  `~/p/DMX-Fixtures-qlctool`). Phases 0 (link trust), 1 (qualify the show,
  generated map, LIVE states and PARAR TODO), 2 (pages, lock, power key,
  damage repaint), 3 (setup without ssh) and 4 (speed: two cards, tap, BPM
  steps, half and double time, Tap both) are done and measured; see
  `TODO_LOG.md` 2026-09-12. What remains is gated on I: Phase 5
  finite accents (the fog and bump decisions below) and Phase 6 position (the
  XY pad's range experiment below). The desk is usable for a show as it is.
- [!] Phase 3's checks that need a finger on the tablet (the desk runs there
  now with `/data/desk.conf` pointing at this Mac's 9998 instance): tap the
  gear, scan, join `TestNet5` with its key (not in the brain; I
  types it on the keyboard) and watch the card come back to the address,
  then join `TestNet` again from its `known` row; drag the fader and read
  `/sys/class/backlight/backlight/brightness`, restart the desk and see the
  level held; the `Dim on battery` toggle. The join transaction is proven
  against a fake supplicant only (`tests/dmx-desk/wifi_join_test.c`); the
  event names it waits for are wpa_supplicant's documented ones. Smallest
  action: I at the tablet with the key, ten minutes.
- [ ] The link banner covers the compact state row on the family pages while
  connecting; it is transient and nothing is pressable then, but a banner
  that sits under the row instead would read better.
- [ ] A page or bank change repaints the whole screen once, about 41 ms on
  the tablet: one frame, off the drag path, so it stays.
- [ ] Bumps (Phase 5) stay rejected as first designed: the reviewer showed a
  SingleShot start does not carry Flash priority or ForceLTP, so a colour
  bump over a running wheel mixes to white, and the vertical smoke scene
  drives four columns. The held hits are carried in the map disabled with
  `held on the Mac`; a purpose-built additive intensity bump is the first
  candidate, and any fog needs my hold and cooldown plus a DMX-output
  proof with the client killed mid-burst.
- [ ] Show mode: the appliance sleeps the screen after five minutes idle, and
  a desk waiting for the next cue is idle. Hold the screen on during a show,
  give the power button a defined behaviour, and cancel every contact on a
  display transition.

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

- [x] Soak the mainline cube: done 2026-09-10, 8 hours unattended on v85. The
  VOP interrupt advanced 168 counts in 3 s at the end of it, exactly the
  panel's 56 Hz, so the display was still flipping rather than merely alive;
  glcube still running, Wi-Fi still associated, battery charged to 100 % and
  holding at a 13 mA trickle. No GPU hang, no `flip_done` timeout, no oops. The
  only recurring message is `rtw88_8723cs: failed to get tx report from
  firmware`, 11 times in 8 hours, which is its own item below.
- [ ] `rtw88_8723cs: failed to get tx report from firmware`, 11 times in an
  8 hour soak (2026-09-10, v85), roughly every 10 to 60 minutes and never in
  bursts. The link stayed associated throughout, so it costs nothing visible;
  worth understanding before the driver work is called finished.
- [ ] A real application in place of the `glcube` demo; what the appliance is
  for is still unstated.
- [ ] Bluetooth: which of the three RTL8723CS firmware variants this board loads
  (`_cg` and `_vf` are byte-identical, `_xx` differs) is not established, and
  nothing has tried BT.
- [ ] `/dev/rga` (2D accelerator) is available and unused; `particles` could
  blit through it instead of the CPU.
