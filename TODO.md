# TODO

> States: `[ ]` pending · `[~]` partial or unverified · `[!]` blocked · `[x]`
> verified complete · `[-]` obsolete or superseded. Closed work moves to
> `TODO_LOG.md`.

Device evidence reviewed 2026-09-13: mainline 7.3.0-rc2 has booted since
v59 and run the cube since v64-v66; v85 records charging policy and an eight-hour
soak, v86 the nineteenth kernel patch, v87 the sleep fix, and v88 the revised
boot-logo palette (its appearance is still unverified). The last recorded BCB
selects `recovery`; the vendor appliance remains the fallback in `boot`.
The exact current partition image hashes were not read during this review.
Boot launches the Vibra DMX desk (`/usr/bin/taq102-desk`, v89 since 2026-09-22),
with glcube as the fallback after five desk exits; `tools/run-dmxdesk.sh` still
pushes a development build over it into `/tmp`. The journal is `docs/journal.md`.
Device as of 2026-09-09: `boot` = `recovery-taq102-v43-appliance.img` (kernel
v40, vendor 4.4.167), `recovery` = `recovery-taq102-v89-desk.img` (mainline
7.3.0-rc2 v86 kernel, v87 ramdisk plus the desk, v88 logo resource), BCB = `boot-recovery`, so every
power-on runs the desk. `tools/loader-watch.sh bcb` puts the vendor
appliance back. The journal is `docs/journal.md`.

## Security

## Bugs
- [x] Fix false desk staleness when QLC+ sends WebSocket Ping/Pong without
  text (2026-09-13). Instrumentation reproduced three drops in ten minutes
  with no preceding loop iteration over 200 ms. The packet-correlated drop
  closed only 605 ms after the desk answered a master Ping; that activity
  was discarded by the text-only silence clock. Count valid control frames,
  preserve one outstanding probe's RTT clock, and keep the 750 ms threshold.
  Replay fails before the fix and passes after it, including a real-silence
  drop at 751 ms. Host gate: 41 PASS, 0 FAIL. Control-frame fix alone: one transport-loss
  drop in ten minutes; the final 100 ms probe cadence had zero drops in
  its completed ten-minute window.
  Evidence: `docs/evidence/2026-09-13-link-regression/README.md`.
- [x] Qualify the desk's 100 ms heartbeat cadence with its unchanged 750 ms
  silence deadline. After fixing control-frame activity, a second captured
  drop had no loop stall: the Mac replied in 7 ms, but that reply and its
  retransmission were not acknowledged before the tablet closed. The desk
  had spent 400 ms waiting to probe and gave transport only 405 ms before
  closing. Probe at 100 ms to reserve about 550 ms including an idle poll;
  do not extend the stale deadline. The 514 ms reply replay fails with the
  old cadence and passes with the new one; real silence still drops at 751 ms.
  Radio/AP/driver attribution is not established; Wi-Fi power save is unchanged.
  Final live window [184972682, 185572682) ms: zero drops, versus three
  before the fix in ten minutes. At 20:15:52 UTC, PID 8050 remained linked
  to the host on port 9998. This short live window is not long-show qualification.
  Evidence:
  `docs/evidence/2026-09-13-link-regression/README.md`.
- [ ] Locate the Mac-side API response delay captured on 2026-09-13:
  513 ms from heartbeat arrival on en0 to QLC+ response, with an early TCP
  acknowledgement and no retransmission. The desk loop was not stalled.
  The previous RTT metric overwrote pending send times and censored replies
  after disconnect, so its small maximum did not exclude this delay.
  Next: correlate QLC+ request dispatch and host scheduling with the retained
  packet timeline without restarting my running show. Two other
  diagnostic drops preceded packet capture and lack packet-level attribution.
  Evidence: `docs/evidence/2026-09-13-link-regression/README.md`.
- [!] The controller host and the tablet must land on the same layer-2 segment
  or the desk cannot link. Measured 2026-09-13 across two access points: the
  host's address was ARP INCOMPLETE from the tablet, unpingable and HTTP-dead,
  while its wired address answered normally, so the second access point is not
  bridging the two clients into one segment. An evening was spent blaming QLC+
  and the finder for what was a network partition.
  Why it matters: at a venue the tablet is Wi-Fi only, and if the host drifts
  onto a different access point mid-show the desk goes dark with both machines
  "on the network". Wired Ethernet as the host's primary (default route,
  service order 1) is what protects against this.
  Smallest action: put both on one access point, or set the second to bridge
  mode, then re-run the finder from the tablet with the host on Wi-Fi only and
  confirm it still lists the host.
- [x] Triage the desk geometry audit (2026-09-13). Brightness now reaches
  8..255 on its painted track, the room/link word is bounded after the tabs,
  exterior coordinates cannot commit touches, and setup arrows are 48x48.
  The detector retains its original thresholds and exhaustive probes; its
  injected 16 px pager strip still fails. Evidence and verification:
  `docs/evidence/`.
- [x] Retain modal setup navigation intentionally. The 80 zero-reach tab
  observations described interception, not missing controls. Tabs are now
  explicitly disabled while setup is open in router and audit metadata.
  Regression obligation: keep the all-tabs modal assertion when routing changes;
  gear, lock and the master column remain available.
- [x] Retain expanded setup list-row hits intentionally. Each of eight audited
  rows accepted 4,000 px beyond its paint (8 px sides, 4 px vertically).
  `desk_setup_row_hit` declares that envelope; the audit verifies it without
  deriving allowance from successful hits. Regression obligation: update the
  declaration with any row-layout change. The old arrow offset was corrected.
- [x] Retain deliberately inert backgrounds and gutters. The original 37 dead
  components mixed empty layout with potential defects. Production inert-mask
  declarations now distinguish them, with hard failures if an exemption covers
  enabled paint or actions. Regression obligation: retain the injected bottom
  16 px pager-strip failure when changing layout or dead-component policy.
- [ ] Hands-on tablet check of geometry fixes: brightness's 8..255 end bands,
  ellipsized long room/link captions and setup's 48x48 paging arrows. Host
  probes and ARM compilation cannot establish physical tap comfort. Evidence:
  `docs/evidence/`. Next: deploy and check only
  when I authorizes an idle window; the linked port-9998 show was not
  disturbed by this task.
- [ ] Extend geometry-audit state/routing coverage. The full-pixel detector
  mirrors the runtime's fresh-contact dispatch in `audit/reach.c`; arbitrary
  network text, every disabled/busy combination, lock transitions and partial
  redraws are not exhausted. Evidence: the original audit's Limits section
  and the triage report. Next: add a busy/disabled setup fixture and a shared
  pure routing seam before changing runtime dispatch, preserving model probes.

- [ ] Reconcile the desk's inert touch-flip configuration and misleading log.
  `src/dmxdesk.c` configures `DMXDESK_TOUCH_FLIP` and prints
  `touch: declared 1663x895, mapping 1024x600, xy`, but never calls
  `touch_input_set_flipped`; the decoder starts with `flipped == 0`, so
  `touch_flip_value` returns after scaling without applying either flip.
  The real mapping is x = round(raw_x * 1024/1664),
  y = round(raw_y * 600/896). `src/glcube.c` does apply its orientation:
  its accelerometer path calls `control_input_reset`, which calls
  `touch_input_set_flipped` in `src/control_input.c`. The inconsistency is
  in dmxdesk, not glcube. Next: decide whether the desk lacks an intended
  setter call or its mode and log are vestigial, then make configuration and
  reporting agree. Leave the working desk unchanged for my show;
  resolve the endpoint bug below before enabling any flip.
- [ ] Correct the flipped-coordinate endpoints in `src/touch_flip.c` before
  enabling the desk's flip. `touch_flip_value` returns `config->height - value`
  and `config->width - value`; these should be `height - 1 - value` and
  `width - 1 - value`. Raw 0 currently becomes y 600 on a 600-tall screen
  (or x 1024 at width 1024), outside the valid pixel range; for valid scaled
  input columns 0..1023, output column 0 is unreachable. This is latent in
  dmxdesk because it never enables flipping, but glcube can reach the shared
  flip path through `control_input_reset`. Next: cover both axes and both
  endpoints with regression assertions, fix the subtraction, and qualify
  orientation before enabling it in the desk. No behaviour change now.
- [ ] Qualify the RK816 input-limit cache fix in mainline patch 0008 on a
  later authorised kernel boot. On 2026-09-13, sysfs reported 1500 mA while
  hardware held 450 mA (0xa1=0x40); an accepted repeat override did not write
  through the nonvolatile regmap cache. One direct 0x45 write restored
  +634..+640 mA charging within 10 s. The patch now makes USB_CTRL volatile,
  forces immediate masked writes, uses live PMIC presence, and expires the
  override in the unplug IRQ. Patch application and the userspace callback
  regression passed; complete kernel compilation and live IRQ/sysfs tests
  remain unverified. Evidence: `docs/evidence/2026-09-13-charging/README.md`.
- [ ] Decide the appliance's automatic known-supply policy after VBUS returns.
  The existing init override runs only at boot, so a mains cut while the tablet
  stays up leaves no automatic reapplication. Recommendation: a supervised
  appliance service watching USB/AC presence with polling fallback, applying
  the explicitly configured limit on return or a live-limit mismatch, verifying
  readback and reporting persistent discharge. No policy implemented; see
  `docs/evidence/2026-09-13-charging/README.md` for scope and verification.
- [ ] `usb2phy` charger detection reports all cable states zero with VBUS
  present and the gadget enumerated (2026-09-10). This does not diagnose wiring
  or bvalid: `EXTCON_USB` is only published for SDP, and peripheral mode does
  not disable detection. Unknown ports stay at 450 mA; the appliance's explicit
  userspace override requests 1.6 A, rounded down to 1500 mA, and unplug clears
  the override. The board's DCP limit is not an automatic unknown-port fallback.
  Evidence: `docs/evidence/2026-09-10-charging/README.md`, review section,
  `c6aaef3`. Next: trace BC1.2 detection with known SDP and DCP sources before
  attributing the all-zero state to a hardware signal.
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
  lines once a minute to `/data/panel-trap.log` (started by hand on 2026-09-12).
  The package now installs `/usr/bin/panel-trap`; next build an image with it
  and add boot startup, then verify a log record after reboot. When it shimmers
  again: note
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
  drain **with 0xa1 already 0x45**, first try
  `i2cset -f -y 2 0x1a 0xa1 0x45` (same value) and read the
  current 15 s later. If that re-arms it, the fix is a periodic rewrite in
  `rk816_charging_monitor` when plugged in and discharging; if only a change
  does, the fix toggles VLIM. Either way it is a driver change we own.
  The 2026-09-13 recovery changed 0x40 to 0x45 and diagnosed a separate stale
  regmap cache; it does not settle this same-value or voltage-limiter question.
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
- [~] Finish the mainline integration. Linux 7.3.0-rc2 boots on hardware
  (v59), Mesa/Lima draws the cube (v64-v66), touch has produced real events,
  and Wi-Fi/charging fixes reached v85-v86. The earlier claim that userspace
  did not exist or the kernel never reached it is superseded by `TODO_LOG.md`
  and `kernel/mainline/README.md`. Still open: reproducible kernel/module
  packaging, the `fw_devlink=off` workaround, regulator-backed GPU OPPs, and
  the remaining driver and hands-on checks below. Next: wire the mainline kernel
  and `modules_install` into the defconfig rather than assembling the ramdisk
  by hand; keep the existing built-in-boot trace task as the deadlock follow-up.
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
- [~] Touch on mainline works in recorded sessions, but intermittent failure
  is not ruled out. On v85 (2026-09-10), IRQ gpio2 15 stayed at 164, the pin
  stayed low, and the controller reported zero fingers over i2c; I
  never confirmed touching inside those measurement windows. That cannot
  establish that the controller never scans. Real finger taps retimed both
  dials on 2026-09-12 (`docs/evidence/2026-09-12-desk-phase4/tablet.txt`).
  Next: capture a timestamped finger/drag test with IRQ and finger-count
  readings at the same time; compare start-up writes only if a confirmed
  contact produces no events.
- [ ] The PLL jitter mechanism is probable, not proven (prediv 12 runs the
  phase detector at 2 MHz, prediv 2 at 12 MHz); a 348 MHz probe on 2026-09-04
  was cut off. Only matters if the panel clock ever changes.

## Appliance

- [ ] Add voltage-based low-battery shutdown after a decision.
  `kernel/mainline/RK816-BATTERY.md`, "The zero algorithm", says the hardware
  can cut out while the driver still reports 3%; voltage must be the trigger.
  No source implements power-off; `src/reboot_target.c` only calls
  `reboot(RB_AUTOBOOT)`. Next: agree a voltage threshold and validation plan
  under show load at the rig, including transient sag and warning time.
  Do not implement or test an unapproved shutdown path during a show.

- [~] Verify wake by power key after the v87 sleep fix (2026-09-11).
  Charger auto-sleep and stillness-based pickup arming are measured and closed
  in `TODO_LOG.md` (`79eea3d`); waking with the power key still needs I
  at the tablet. The Vibra asset decodes correctly, but U-Boot's rendering is
  not accepted: the separate smudge item below remains blocked.
- [!] The new boot logo draws as a smudge (observed 2026-09-11, v87). The BMP is
  provably well formed -- 600 rows of exactly 1024 pixels, runs only, the
  vendor's own palette since v88 -- and Pillow decodes it back correctly. That
  proves the asset decodes, not
  that U-Boot renders it correctly; the source of the smudge remains unverified.
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

- [ ] `loader-watch.sh` still polls `rkdeveloptool` with no per-call timeout, so
  any future libusb wedge hangs it rather than reporting one. The Pioneer daemon
  that used to cause it is disabled now (`TODO_LOG.md`, 2026-09-13), which
  removes the known trigger but not the failure mode. Smallest action: a
  per-call `timeout` around `rkdeveloptool` and a line saying what it means.
- [ ] Mainline `u_serial` warns in `gs_close()` when the host vanishes with
  the ACM console open (seen when the Mac rebooted). Harmless; a report for
  the gadget list some day.

## DMX desk (Phase A)

The tablet as a control surface for the Vibra QLC+ show, with the show
MacBook as master. Design and evidence:
`docs/journal.md`, the reviewer review in
`docs/evidence/`.

- [ ] Check the unterminated mainline defconfig patch-directory assignment.
  `br2-external/configs/taq102_mainline_defconfig` contains
  `BR2_GLOBAL_PATCH_DIR="` with no closing quote (already present at HEAD).
  The desk package was validated using the existing VM configuration plus
  `olddefconfig`, not a fresh defconfig import. Next: test a fresh import in an
  isolated output directory and normalize the intended empty string if needed.
- [ ] Hide SHOW's empty Fijo heading when the map has no fixed accent toggle.
  The ed1dac1 map removed `color-beam`; `src/desk_show_layout.c` still emits
  Fijo unconditionally. The host `desk.ppm` shows an empty label to the right
  of five hits; all LIVE controls are placed and geometry passes unchanged.
  Next: emit that heading only for a placed toggle, then rerun renderer and
  geometry checks without relaxing their thresholds.
- [ ] Implement the fog cooldown policy once its exact timing is settled.
  I-approved cooldown is not built: `src/desk_build_holds.c` registers
  every burst, including fog, with cooldown 0 (formerly in `dmxdesk.c`).
  The local hold model can express cooldowns, but a master-side burst bound
  alone does not impose a minimum interval between bursts. Next: confirm the
  duration and enforcement point at the rig, then test retriggering and
  reconnects with the rig; keep the master-side duration bound.
- [~] Qualify the finite fog bursts with the rig. All 17 bounded bursts,
  including HUMO YA and HUMO VERT, ship in `show/vibra.desk.json` with
  master-side SingleShot bounds (`fca61bf`, `8892dcc`, `ecc3379`). The former
  requirement to implement a finite fog cue is complete. Still owed: observe
  physical DMX output reaching zero by the deadline with the client killed
  and Wi-Fi pulled during ON. Next: the hands-on rig session below; do not
  substitute a websocket echo for output measurement.
- [~] Finish protocol/output qualification against the generated Vibra map,
  `show/vibra.desk.json`: room states, family picks, grand master w246,
  PARAR TODO w21 and dials w34/w274. Solo handoff and reconnect have recorded
  bench evidence (`docs/evidence/2026-09-12-desk-phase1/handoff.txt`); finite
  starts/stops have `docs/evidence/2026-09-13-desk-bursts/`. Next: measure
  burst deadlines and colour/shutter priority at actual DMX output in the
  hands-on rig session. Position and attribute takeover remain separate
  experiments against this show's own widget and function ids.
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
- [~] Qualify the generated `Vibra.qxw` gig copy from the show repository's
  `qlctool` worktree. Its map already carries 132 controls, seven pages, two
  dials and 17 bounded bursts; generation is complete. Next: agree durations
  and colour/shutter priority at the rig, then regenerate and validate the
  map with the approved show and bind it to the workspace actually loaded.
  Position/colour-attribute additions need their own output-range evidence.
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
- [~] Verify the heartbeat's untouched-scene acceptance on the current
  Vibra show. The heartbeat implementation and RTT are proven: 2744 samples
  over 30 minutes, p50 19 ms, p95 47 ms, max 319 ms, no drops. But
  `docs/evidence/2026-09-12-desk-phase1/heartbeat-rtt-30min.log` identifies
  `deluxe-eventos` with only 1 of 11 controls enabled, not a ten-minute held
  Vibra scene. Next: run an enabled current-show scene untouched for ten
  minutes and retain the link and scene-state log.
- [ ] Bind validation to the workspace the master actually loaded. The show
  repository already generates the manifest/map; `src/showmap.c` reads its
  `show.sha256`, but no runtime code compares it against the master's loaded
  workspace. Matching widget ids cannot prove matching channel outputs or
  dependencies. Next: define an authoritative loaded-workspace identity from
  the master and reject a mismatched map before enabling controls; add a
  same-ids/different-workspace regression once that source is established.
- [ ] Keep a stopping policy for every future control added to the generated
  Vibra map. The current 17 bursts are master-bounded; latched states and
  picks deliberately run until changed or stopped. Next: extend the show's existing generator checks whenever a new action type is
  proposed, proving its stop/timeout and disconnect behaviour at the master.
- [~] The desk as a professional busking surface: the plan is
  `docs/journal.md` (the reviewer reviews in
  `docs/evidence/` and
  `docs/evidence/`). Approved 2026-09-12.
  The show is `Vibra.qxw` on DMX-Fixtures' `qlctool` branch (worktree
  the fixture tool repository). Phases 0 (link trust), 1 (qualify the show,
  generated map, LIVE states and PARAR TODO), 2 (pages, lock, power key,
  damage repaint), 3 (setup without ssh) and 4 (speed: two cards, tap, BPM
  steps, half and double time, Tap both) have implementation and bench
  evidence; see `TODO_LOG.md` 2026-09-12. Phase 4's acceptance gate remains
  unmet: the spec requires touch-to-DMX p95 under 100 ms, but
  `docs/evidence/2026-09-12-desk-phase4/tablet.txt` records touch-to-ECHO only,
  seven samples 39/40/45/56/58/145/179 ms (nearest-rank p95 179 ms).
  Next: measure timestamped touch-to-DMX with enough samples and resolve the
  tail before accepting the phase. Phase 5's 17 finite accents are built;
  rig timing/HTP priority and cooldown remain open, as does Phase 6 position.
  Show readiness is not established by these bench checks alone.
- [!] The second design's checks that need a finger on the tablet (the desk
  runs there with SHOW first, every hit a bounded burst): hold FLASH and see
  the light while held and the tile amber, lift and see it off; hold past
  eight seconds and see the master end it by itself; HUMO YA and HUMO VERT
  the same at three seconds; two fingers on two hits; tap a state, a colour,
  an ambient rhythm and OFF; TAP on the tempo card; the tab strip and the
  pager; the gear, Cerrar, the keyboard; the lock target in the bar. Then
  the durations: 8 s for the flashes and the colour hits, 4 s for the
  strobes, 3 s for the fog are provisional, in `BURST_MS` in the show
  repository's `desk_policy.py`. Smallest action: I at the tablet
  with the rig on, fifteen minutes.
- [ ] Second design leftovers, from the reviewer's critique of the built frames
  (`docs/evidence/`): the burst durations
  and their priority over a running state want a rig session (the reviewer's
  per-hit judgment is in `docs/evidence/`:
  fog and full-intensity flashes win HTP; colour hits can mix to white over
  a complementary state, strobes can lose a shutter to a running chase);
  the master's readout is above the travel now but the pressed
  request marker and the level's word still want a look on the tablet under
  a finger; segmented discs have stepped edges (no anti-aliasing in the
  round-rect primitive); accents in the show's own captions ("AUTO rapido",
  "Circulo") are the show's to fix; CABEZAS heading "AUTO" covers "Centro" too.
  The running-room word overlap is fixed and covered by the geometry audit
  (2026-09-13), including a maximum-length room caption.
- [ ] `wpa_ctrl_abandon` that fails to redial leaves the request channel dead
  for the rest of the run (pre-existing: no reconnect on a dial failure;
  the reviewer of the asynchronous work noted the exposure grew since
  abandon now runs on every timeout). Smallest action: a bounded retry of
  the dial on the next request.
- [ ] Supplicant lifecycle waits remain outside the desk loop:
  `wpa_ctrl_open` waits up to 1 s for `ATTACH`, and `wpa_ctrl_close` waits up
  to 200 ms for `DETACH` (including cleanup after a failed open). Runtime
  card, join, and rollback requests are asynchronous. Evidence:
  `src/wpa_ctrl.c`, `src/wpa_ctrl_close.c`, and
  `docs/evidence/`.
  Smallest next step: make event attachment asynchronous so an unresponsive
  daemon cannot delay touch startup; send DETACH without waiting at exit.
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
- [~] Resolve burst priority over a running state (Phase 5). All 17 hits now
  ship enabled as bounded bursts, not disabled held controls. SingleShot does
  not confer Flash priority or ForceLTP: complementary colour hits can mix
  to white, strobes can lose the shutter to a chase, and vertical smoke drives
  four columns. Evidence: `docs/evidence/`.
  Next: use the hands-on rig session to choose additive intensity or another
  show policy per hit and verify restoration of the underlying look.
- [ ] Show mode: the appliance sleeps the screen after five minutes idle, and
  a desk waiting for the next cue is idle. Hold the screen on during a show,
  give the power button a defined behaviour, and cancel every contact on a
  display transition.

## Pending decisions

- [!] `GLCUBE_FINISH=1` (a `glFinish` between `eglSwapBuffers` and scanout)
  stays a diagnostic; the black flashes were the accelerometer i2c read in the
  render loop, since fixed. Decide after a long run whether to drop the switch.
  Blocked: observation over a long run: any black frame with the default build?
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

- [ ] `rtw88_8723cs: failed to get tx report from firmware`, 11 times in an
  8 hour soak (2026-09-10, v85), roughly every 10 to 60 minutes and never in
  bursts. The link stayed associated throughout, so it costs nothing visible;
  worth understanding before the driver work is called finished.
- [ ] Bluetooth: which of the three RTL8723CS firmware variants this board loads
  (`_cg` and `_vf` are byte-identical, `_xx` differs) is not established, and
  nothing has tried BT.
- [ ] `/dev/rga` (2D accelerator) is available and unused; `particles` could
  blit through it instead of the CPU.
