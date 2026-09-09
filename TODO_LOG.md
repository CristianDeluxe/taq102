# TODO Log

> Searchable record of closed project work. Active work lives in `TODO.md`.

## 2026

### 2026-09

- [x] 2026-09-09 — **Mainline:** touch geometry measured and applied. The
  GSL3673 reports a 1664x896 grid with Y inverted; the DTS says so and
  glcube scales the declared range to the panel (v74). glcube also finds
  its input nodes by device name. Evidence: raw captures decoded in
  `kernel/mainline/FINDINGS.md`, "Touch works, once the geometry is told".
- [x] 2026-09-09 — **Mainline:** MAINTAINERS entry for `rk816_charger.c`
  (in 0008); the board DTS needs none, `get_maintainer.pl` already routes
  it to the ARM/Rockchip entry. Series re-verified: `git am` 17/17, tree
  equal to the VM's, `checkpatch --strict` clean bar the added-file
  reminder on 0017.
- [x] 2026-09-09 — **Mainline:** series taken through the tools. `checkpatch
  --strict`, `dt_binding_check` and `dtbs_check` (dtschema installed in the
  VM) found: no schema for the board compatible, forbidden reboot modes
  under the PMU, the D-PHY's extra clocks outside their binding, bindings
  inside driver patches, and four style nits. All fixed in the tree; the
  series is 17 `git am`-able patches reproducing it (0 diff lines); v72
  and v73 run it on the tablet. Left: the panel part number and MAINTAINERS.
- [x] 2026-09-09 — **Mainline:** patch series reviewed and rebuilt. Findings
  acted on: 0011 conflicted with 0007; no messages or Signed-off-by on most
  patches; PHY leak on the rk312x LVDS probe error paths; no DT binding for
  `rockchip,rk3126-lvds`; silead NAK tolerance unscoped; RK816 message
  stale and a division that could reach zero; stale numbering in README and
  config. Series is now `git format-patch` output, 12 patches, `git am`
  applies all twelve onto `28924df2a` and the tree equals the VM's (0 diff
  lines). v69 built from it runs on the tablet.
  - Evidence: `docs/evidence/2026-09-09-cube/v69-console-reviewed-series.log`.
- [x] 2026-09-09 — **Mainline:** the cube froze after 73 s on v66. lima's
  devfreq (`simple_ondemand`) drove the Mali from the bootloader's 148.5 MHz
  up to 480 MHz over the rk3128.dtsi OPP table with no regulator attached;
  150 transitions, then `pp0 job timeout` every 10 s and no reset recovered
  it. v67 deletes `operating-points-v2` from `&gpu`; 148.5 MHz, no devfreq,
  52.8 FPS, 0 timeouts at 2.5 min and at 23 min (soak sampled every
  5 min).
  - Evidence: `docs/evidence/2026-09-09-cube/v66-gpu-hang-devfreq-480mhz.txt`,
    `v67-console-no-devfreq.log`.
- [x] 2026-09-09 — **Mainline:** the cube runs. v66 boots Linux 7.3.0-rc2 and
  starts `glcube` by itself 14 s after power-on: Mesa 26.0.1 lima on the
  Mali-400 MP2, 1024x600, 52.8 FPS, `glGetError 0x0`.
  - Resolution: kernel = variant M unchanged; ramdisk gained `gpu-sched.ko`
    (the symbol lima was missing) and `taq102-cube`; board DTS gained
    `&gpu { status = "okay"; }` (rk3128.dtsi ships it disabled).
  - Evidence: `docs/evidence/2026-09-09-cube/`, images v64-v66 and
    `zImage-7.3.0-rc2-variant-M` in the archive with SHA256SUMS.
- [-] 2026-09-09 — **Mainline:** `GENPD_FLAG_NO_SYNC_STATE` as the narrow fix
  for the genpd deadlock. v63 (display stack built in plus that flag) hangs
  before userspace exactly like v62 without it. Superseded by the open item to
  trace a built-in boot; `fw_devlink=off` with the PHY as a module stays.
- [x] 2026-09-08 — **Mainline:** the panel works. Kernel console on the
  tablet's own screen at 1024x600, LVDS connector connected with the right
  physical size; two mainline bugs found (genpd deadlock, LVDS panel-bridge
  hijack), written up in `kernel/mainline/ISOLATING-THE-DISPLAY-HANG.md`.
- [x] 2026-09-08 — **Mainline:** Linux 7.3.0-rc2 boots on the tablet (v59):
  RK816 battery, eMMC and /data, rtw88 with a DHCP lease, USB ACM console.
  Evidence: `docs/evidence/2026-09-08-mainline/`.
- [x] 2026-09-08 — **Recovery:** the button dance landed after the v54 white
  screen, the BCB was zeroed and v49 came back with glcube running; the
  mainline crash log was not recovered (ramoops does not survive the power
  cycle the dance needs).
- [x] 2026-09-07 — **Appliance:** The status bar's battery and Wi-Fi icons were
  oversized and heavy, "nothing like iOS" in my words after v48.
  - Resolution: 5 by 11 units instead of 13 by 7, radius h/3, outline h/10,
    the Wi-Fi sector widened to 55 degrees each side with thinner arcs, gaps
    tightened, and the real systemGreen and systemRed
    (`src/statusbar.c`).
  - Evidence: photographed off the panel's own scanout before and after,
    `docs/evidence/2026-09-07-control-centre/statusbar-icons-before-after.png`;
    17 host tests pass; shipped as v49 (build `20260907-225750-25be25d`),
    written to `boot` and read back with SHA-256 matching.

- [x] 2026-09-07 — **Appliance:** The control centre's Wi-Fi tile, unseen on
  glass when v48 shipped.
  - Resolution: read off the panel's framebuffer with the control centre open.
    It shows `Wi-Fi`, `TestNet`, `192.168.1.51` and `-35 dBm`
    (`docs/evidence/2026-09-07-control-centre/panel-open-wifi-connected.png`).

- [x] 2026-09-07 — **Bugs:** The control centre offered "Turn on" for a Wi-Fi
  that was already associated, and the tap that followed killed the Wi-Fi.
  `taq102-wifi up` and the app are both `::once` entries in inittab, so the
  runtime latched `wanted_wifi` from a status read taken before the link
  existed and never revised it. Tapping the wrong state ran `taq102-wifi up` a
  second time; `load()` returns early when `wlan0` exists, so a second
  `wpa_supplicant` started and the two reset each other's association for
  nearly two hours.
  - Fixes: `read_status` adopts an observed link as the wanted state except
    while an action of its own is in flight (`src/control_runtime.c`); `up`
    exits when the interface is already associated and addressed and otherwise
    clears leftovers first, and passes `-O /var/run/wpa_supplicant` because
    `wpa_passphrase` writes no `ctrl_interface=` line
    (`br2-external/package/taq102-wifi/taq102-wifi`); `read_wifi` ignores the
    -256 dBm an unassociated interface reports, which had been lighting a bar
    on a radio connected to nothing (`src/status.c`).
  - Evidence: the new case at `tests/control-centre/runtime_test.c:117` fails
    without the runtime fix and passes with it; 17 host tests pass. On the
    tablet, v48 (build `20260907-222214-8066348`) written to `boot` and read
    back byte for byte, SHA-256 matching, kernel and resource identical to
    v47. From a clean boot: Wi-Fi up by itself at 192.168.1.51, -35 dBm, one
    `wpa_supplicant`, one `udhcpc`, one `mdnsd`; a second `taq102-wifi up`
    answered "already associated and addressed" and left the count at one; and
    `taq102-wifi status` reached the daemon for the first time,
    `wpa_state=COMPLETED`.

- [x] 2026-09-07 — **Bugs:** the reviewer's review of the whole control-centre change
  (session `01a07d7f`) found three defects, fixed and shipped as v47: the
  rescue reboot skipped `sync()` (pending `/data` writes could be lost);
  the brightness sample was judged with the frame's timestamp, older than
  the sample's own, so the policy reset its window every time and Auto
  could stay at 255; and the touch decoder reset its slot to 0 on a flip or
  a cancel while the kernel kept slot 1, which could lose the first finger
  of a pinch.
  - Evidence: `test_slot_selection_survives_flip_and_cancel` in
    `tests/control-centre/touch_test.c`; 17 host tests pass; v47 (build
    `20260907-202158-bb04db0`) flashed to `boot` and `recovery`,
    readback-verified, 54.8 FPS from the clean boot, BCB zero.
  - Files: `src/reboot_target.c`, `src/control_runtime.c`, `src/touch_input.c`.

- [x] 2026-09-07 — **Appliance:** The control centre, the brightness policy,
  idle sleep with pick-up wake, and Inter on every screen (v46 in `boot` and
  `recovery`).
  - Result: spec `docs/2026-09-07-control-centre-design.md`
    (reworked after a the reviewer review with ten blocking issues), plan
    `docs/2026-09-07-control-centre.md`, twelve tasks;
    the reviewer implemented tasks 1 to 11 in five runs, each committed with its
    tests; new modules `font`, `canvas_blend`, `settings`, `backlight`,
    `power_policy`, `sleep_state`, `touch_input`, `touch_router`,
    `control_center` (model, layout, painter), `action_worker`,
    `wifi_status`, `reboot_target`, `touchsim`; `glcube.c` down to 627
    lines of wiring. Rescue backlight boots at 40.
  - Evidence: 17 host tests under ASan/UBSan pass
    (`tools/test-control-centre-host.sh`); the device test on the clean v46
    boot passes 22 of 22 (`docs/evidence/2026-09-07-control-centre/device-test-v46.log`,
    scanouts `open.png`, `mid-drag.png`, `armed.png`, four 60 s phases at
    54.8 FPS, zero GL errors); rescue round trip on v46: backlight 40 of
    255 logged by `/init`, battery +9 mA on the CDP port instead of -292,
    `rescue-screen text: Inter`, dump `rescue-v46-dump.png`, BCB zeroed and
    the appliance back at 54.8 FPS. Flashes readback-verified; images
    archived as `recovery-taq102-v46-*.img`.
  - Files: `src/`, `tests/control-centre/`, `tools/test-control-centre-*.sh`,
    `br2-external/package/taq102-fonts/`, `README.md` section "The control
    centre".

- [-] 2026-09-07 — **Pending decisions:** Backlight default.
  - Resolution: I wants it to adjust itself; the brightness
    controller and the manual slider replace a fixed default.

- [x] 2026-09-07 — **Kernel:** v44 and v45 flashed and proved: kernel with
  patches 0006 and 0007, the reset-pulse PHY module, and the new user space in
  `boot`; the matching stock-kernel rescue in `recovery`; BCB round trip done.
  - Result: v44 booted first with the old module still in `blobs/` (patch 0007
    absent), so `blobs/phy-rockchip-inno-video-combo-phy-4.4.167.ko` was
    replaced by the v44 build (md5 `0739dbab`) and v45 packed and flashed.
    From a clean v45 boot: build `20260907-162319-23127e9`, `4.4.167`, module
    md5 on the tablet `0739dbab`, PHY registers `REG03=0x02 REG04=0x1C`
    (336 MHz), `REG00=0x7D REG01=0xE0 E4=0xAA` (analog on, defaults after the
    reset pulse), LVDS bound at 4.40 s, glcube 54.8 FPS, `CDP1.5A input=1500`
    kept; fb blank/unblank cycle drops and restores the GSL3673 reset pin and
    the chip answers with the IRQ count climbing 6 to 13. Round trip:
    `boot-recovery` at raw sector 32800, rescue up on 4.4.103 with the screen
    turned by the sensor (y 943 mg), zeroed, appliance back. Camera:
    `docs/evidence/2026-09-07/`. Both flashes readback-verified; the `boot`
    write used `tools/flash-boot.sh`, `recovery` used the new `--no-bcb` mode;
    `tablet.sh reboot-loader` returned in 0.75 s and the loader appeared 4 s
    later (the 2026-09-03 hang is gone). The webcam renders the rescue
    screen as a green-to-pink gradient under BOTH kernels while the scanout
    buffer under ours measures amber and opaque, so the gradient is panel
    angle plus camera, not the VOP; my eyes are the final word.
    Images archived in
    `/Volumes/Datos4TB2/denver-taq102/gate3-build/recovery-taq102-v4[45]-*.img`.
  - Files: `blobs/`, `br2-external/`, `log/v44/` (untracked artifacts).

- [x] 2026-09-07 — **Integrations:** The tablet answers to `taq102.local`:
  hostname set in `/init`, sent to DHCP as option 12, and `mdnsd` (Buildroot
  package, SSH service) started on `wlan0` once the lease is in.
  - Evidence: `dns-sd -G v4 taq102.local` on the Mac returns 192.168.1.57
    within a second on both the appliance and the rescue; ssh by that name
    used for every check after the flash.
  - Files: `br2-external/configs/taq102_defconfig`,
    `br2-external/package/taq102-wifi/taq102-wifi`,
    `br2-external/board/taq102/rootfs-overlay/init`.

- [-] 2026-09-07 — **Security:** Rotate the Wi-Fi key exposed in a 2026-09-02
  transcript.
  - Resolution: owner accepts it (home network, private transcript).

- [-] 2026-09-07 — **Pending decisions:** Mainline track or appliance polish.
  - Resolution: owner leaves it to the run; the appliance goes first (the
    control centre, brightness policy, sleep). Mainline stays a future idea.

- [-] 2026-09-07 — **Infrastructure:** Raise the build VM's memory.
  - Resolution: owner leaves it to the run. OrbStack gives 8 GB overall on a
    16 GB Mac; a full `make` passed at that size on 2026-09-07 with `kbuild`
    stopped, so nothing is raised. Keep `kbuild` stopped while building.

- [x] 2026-09-07 — **Bugs:** the reviewer review of the run (session `01a07c22`)
  found two regressions of mine, both fixed: glcube's bar canvas was reused
  without clearing, so the new composite kept old digits (778 stale pixels
  on a repaint from 87% charging to 12%); it is now cleared before each
  paint. `measure.sh` used an `mktemp` template with a suffix, which macOS
  takes literally, so a second run with the same tag failed; it now makes a
  unique directory.
  - Evidence: host test linking `src/statusbar.c` and `src/canvas.c`: repaint
    versus fresh 0 differing pixels, rescue bar rows 0 alpha-0 pixels; glcube
    rebuilt after `tools/vm-hash-check.sh` caught a stale mount once, deployed,
    54.8 FPS; `mktemp -d` twice with the same template gave two directories.
  - Files: `src/glcube.c`, `tools/panel-camera/measure.sh`.

- [x] 2026-09-07 — **Kernel:** Patch series reconciled: `0001` had absorbed
  `0002`'s two analog-power hunks, so a fresh checkout could not take the
  series in order. `0001` regenerated as the tree minus `0007` minus `0002`.
  - Evidence: in the VM, pristine `HEAD` file + `0001` + `0002` + `0007`
    is byte-identical (`cmp`) to the driver the kernel is built from; before,
    `0002` reported "2 out of 2 hunks ignored". README's stale
    `0001-video-combo-phy-enable-h2p-clock.patch` name corrected.
  - Files: `kernel/patches/0001-video-combo-phy-clocks-and-pll.patch`, `README.md`.

- [x] 2026-09-07 — **Kernel:** Backlog run, wave 3 (the reviewer, session
  `01a07c17`): patches 0006 (GSL3673 releases slots 0..10 on suspend and
  resume) and 0007 (combo PHY pulses its reset at power-on) written, applied
  to the VM tree, built and staged; NOT flashed.
  - Evidence: `tools/build-kernel.sh` from `/work/kernel-v40.config` with the
    hybrid DTS ended with `zImage is ready`, no `error:`; the PHY module
    relinked with vermagic `4.4.167 SMP preempt mod_unload modversions ARMv7
    p2v8`; both patches apply in reverse with zero fuzz; `log/kernel-v44/`
    holds the three artifacts, `shasum -a 256 -c SHA256SUMS` OK on the Mac.
    the reviewer's brief had the slot bound wrong (0..9); corrected to 0..10 after
    reading `input_mt_init_slots(MAX_CONTACTS + 1)`, rebuilt.
  - Files: `kernel/patches/0006-*.patch`, `kernel/patches/0007-*.patch`.

- [x] 2026-09-07 — **Infrastructure:** `tools/vm-hash-check.sh` compares the
  tracked sources on the Mac and through the VM's mount before a build; the
  README build notes name `<pkg>-dirclean` as the resync.
  - Evidence: `tools/vm-hash-check.sh` run against `src` and `br2-external`
    (result in the run report).

- [-] 2026-09-07 — **Kernel:** Put the touch panel's `screen_max_x/y` in the
  hybrid device tree.
  - Resolution: the GSL3673 driver never reads them (only compile-time
    `SCREEN_MAX_*` variants in `gsl3673.h`); the 2048x1536 range is the
    driver's, and `src/touch_flip.c` mapping by the KMS mode is the fix.

- [x] 2026-09-07 — **Bugs:** The rescue screen was washed out under the
  stock kernel: the status bar's downsample rewrote every pixel of the canvas
  it was given, and outside the bar the source is all alpha 0, so the amber
  background came back as transparent black (since the supersampling of
  2026-09-04; the v43 round-trip photo shows it). `statusbar_paint` now
  composites only the bar's rows over the canvas with straight-alpha "over",
  which leaves glcube's transparent bar canvas exactly as before.
  - Evidence: rescue-screen deployed live on the tablet, `/dev/mem` scanout
    read: 614,400 of 614,400 pixels at alpha 255, 504,273 amber, 47 distinct
    colours in the bar rows; glcube redeployed, 54.8 FPS. Found by the reviewer in
    the wave-2 run.
  - Files: `src/statusbar.c`.

- [x] 2026-09-07 — **Appliance:** Backlog run, wave 2 (the reviewer, session
  `01a07c06`): rescue screen rotates with the accelerometer (`RESCUE_FLIP=0|1`
  override), `RESCUE_DUMP` refuses symlinks, `glcube` and `rescue-screen` take
  DRM master explicitly and exit when refused, `particles` paints opaque
  pixels, BusyBox gains `timeout`, `taq102-app` logs to `/data/log`.
  - Evidence: cross-built in the VM with no new warnings; deployed live to the
    tablet's tmpfs with hash checks: second `glcube` exits with
    `drmSetMaster: Invalid argument` while the first keeps 54.8 FPS; with
    fixed telemetry the `RESCUE_FLIP=1` dump equals the `RESCUE_FLIP=0` dump
    rotated a half turn at every pixel, and the unforced run equals the
    flipped one because the sensor reads Y +989 mg; `/dev/mem` scanout of the
    new `particles` has 614,400 of 614,400 pixels at alpha 255; the new
    busybox lists `timeout` and `timeout 1 sleep 5` exits 143; the new
    supervisor wrote 54.8 FPS lines to `/data/log/taq102-app.log`. Appliance
    restored at 54.8 FPS. Full record: the reviewer `FINDINGS-A.md` of the run.
  - Files: `src/rescue-screen.c`, `src/glcube.c`, `src/particles.c`,
    `br2-external/package/rescue-screen/rescue-screen.mk`,
    `br2-external/configs/taq102_defconfig`,
    `br2-external/board/taq102/busybox.fragment`,
    `br2-external/board/taq102/rootfs-overlay/usr/bin/taq102-app`.

- [x] 2026-09-07 — **Infrastructure:** Backlog run, wave 1: repo-only items.
  - Result: `get-rkdeveloptool.sh` pinned to `304f073` and `get-mkbootimg.sh`
    to `d2bb0af`; `flash-recovery.sh` gained `--no-bcb` and both flash scripts
    default to `tools/vendor/rkdeveloptool`; `measure.sh` uses `mktemp`;
    the shimmer instruments (`zigzag.py`, `shift.py`, `sweep.sh`, `wobble.sh`,
    `src/bartest.c`) moved out of the session scratchpad into
    `tools/panel-camera/` with portable paths, `sweep.sh` recording the iPhone
    device and crop; `taq102-app` comment names the measured `KEY_BACK`.
  - Evidence: `sh -n` clean on every script; `tools/flash-recovery.sh --no-bcb`
    prints usage and finds the vendor binary (`rkdeveloptool ld` ran).
  - Files: `tools/get-rkdeveloptool.sh`, `tools/get-mkbootimg.sh`,
    `tools/flash-recovery.sh`, `tools/panel-camera/*`, `src/bartest.c`.

- [x] 2026-09-07 — **Documentation:** README "Status" points at the current
  state; the first DTS's bus-format comment corrected and the file marked
  superseded; the 2026-09-01 the reviewer research archived in `docs/research/`;
  the brain page's "Still open" list drops the two settled questions.
  - Evidence: `docs/research/2026-09-01-review-route-findings.md` (607 lines,
    from the archive disk); brain commit `e79d20d9`, pushed.

- [x] 2026-09-06 — **Documentation:** The two 2026-09-04 the reviewer runs' findings
  archived with the evidence.
  - Evidence: commit `52de391`; `docs/evidence/2026-09-05/review-findings-1-blackframes-touch.md`,
    `review-findings-2-lvds-variants.md`.

- [x] 2026-09-05 — **Infrastructure:** v43: the diagnostics ship in the image
  and `recovery` holds the matching stock-kernel rescue, proved by a BCB round trip.
  - Result: `package/taq102-diag` (`testpattern`, `phytune`, `lvdsdiag`);
    `recovery` written with `rkdeveloptool wl 196608` and read back byte for
    byte; `boot-recovery` at raw sector 32800, rescue up on 4.4.103 with Wi-Fi
    and ssh, sector zeroed, appliance back at 336 MHz.
  - Evidence: commits `c0c9b28`, `6514c1a`; `docs/evidence/2026-09-05/rescue-v43-round-trip.jpg`.

- [x] 2026-09-05 — **Kernel:** The shimmer was the LVDS PHY PLL's jitter; the
  vendor divider pair fixes it (v42).
  - Result: `prediv 2 / fbdiv 28` = 336 MHz instead of `12 / 175` = 350 MHz;
    picture movement 0.005 px rms against 0.63-0.81, 0.06 from a cold boot.
    A stray `REGE4 = 0x80` write living only in the VM tree was removed and
    patch 0001 matches the tree again. The "7x pixel clock" rule was measured
    with the panel unpowered and is retired.
  - Evidence: commit `192cde9`; `docs/evidence/2026-09-05/vlines-*.png`;
    `src/testpattern.c`, `src/phytune.c`, `src/lvdsdiag.c`.

- [x] 2026-09-04 — **Backend:** the reviewer runs: accelerometer read moved off the
  render thread (the black flashes), touch flip mapped to the KMS mode with
  `GLCUBE_TOUCH_FLIP`, `lvdsdiag` variant harness.
  - Evidence: `src/accel_monitor.c`, `src/touch_flip.c`, `src/lvdsdiag.c`;
    findings in `docs/evidence/2026-09-05/`.

- [x] 2026-09-04 — **Appliance:** Wobble fixed and the tablet knows which way
  up it is (v41).
  - Result: the resting spin's 0.84 s kick is held constant; the i2c-2 0x18
    sensor is a Silan SC7A20, driven by `src/accel.c`, and glcube flips
    picture, bar and touch on Y gravity; status-bar margin, bolt, supersampling.
  - Evidence: commit `444cf39`; owner confirmed wobble, orientation and bar
    2026-09-05.

- [x] 2026-09-04 — **Bugs:** Touch alive on the own kernel, cube no longer
  freezes (v40).
  - Result: fbcon's `consoleblank=600` reset the GSL3673 through the fb-blank
    notifier (`taq102-app` unbinds fbcon); the vendor tree's `gsl3673.h` was
    another panel's firmware (patch 0005 installs the stock arrays); a slot
    silent for 0.5 s counts as lifted; KEY_POWER sleep/wake; status bar in glcube.
  - Evidence: commit `242e478`; GSL IRQ count climbing on `/proc/interrupts`;
    owner confirmed touch, button and no flicker 2026-09-04 17:25.

- [x] 2026-09-04 — **Kernel:** Charger limit kept on DC detect (patch 0004, v38).
  - Result: rk816's DC-detect path no longer overwrites the USB detection's
    1500 mA with 450 mA; +584 mA at brightness 255 from power-on.
  - Evidence: `kernel/patches/0004-rk816-battery-keep-usb-input-limit-on-dc-detect.patch`.

- [x] 2026-09-04 — **Kernel:** Flicker root-caused to the VOP's IOMMU; dropped
  (v37).
  - Result: `fliptest` split the flip path from the content (two identical
    buffers flicker, the same buffer does not); `iommus` removed from the vop
    node, CMA buffers at 0x88600000, 54.8 FPS kept. Mainline's rk3128.dtsi has
    no VOP IOMMU either (Alex Bee, LKML 2023-12-16: silicon bug).
  - Evidence: `src/fliptest.c`; `tools/make-hybrid-dts.py` graft 7;
    `kernel/rk3126-taq102-hybrid.dts`.

- [x] 2026-09-04 — **Appliance:** Rescue screen shows battery and Wi-Fi (v35),
  then an iOS-style status bar (v39); glcube keeps a resting spin (v36).
  - Result: the Mac's USB port drains the tablet at brightness 255 (`NONE
    USB` 450 mA) and a USB-C hub charges it (`CDP1.5A`); the start-up spin was
    coasting to a stop in 5 s.
  - Evidence: commits `c50d311`, `70968c1`; `docs/evidence/2026-09-04/`;
    `RESCUE_DUMP` frame.

- [x] 2026-09-03 — **Appliance:** Rescue has a face (v34).
  - Result: `rescue-screen` paints RESCUE MODE, kernel, build id and address
    from a DRM dumb buffer; the stock VOP blends XRGB as ARGB (alpha 0xFF); the
    stock kernel's first modeset is blank, so the CRTC is cycled once.
  - Evidence: commit `8fa9f47`; `docs/evidence/2026-09-03/camera/2026-09-03-v34-*.jpg`.

- [x] 2026-09-03 — **Infrastructure:** The appliance runs on the own kernel
  (v31) and `recovery` holds a real stock-kernel rescue, round trip measured.
  - Result: `taq102-display` loads the PHY module from `/init`; `taq102-app`
    waits for `/dev/dri/card0`; cube at 5.10 s from power-on. v18 had been our
    own kernel without the PHY, a blind rescue. `flash-recovery.sh` verifies by
    readback before writing the BCB. The volume button does not boot
    `recovery`; it trips the autostart hatch (`KEY_BACK`).
  - Evidence: `docs/evidence/2026-09-03/camera/2026-09-03-v30-appliance-boot.jpg`,
    `2026-09-03-recovery-stockkernel.jpg`.

- [x] 2026-09-03 — **Kernel:** The panel had no power: RK816 LDO6 was switched
  off by the regulator core (the stock kernel's disable fails and that is what
  kept it alive); then the first modeset left GPIO2_B4 low (patch 0003). v29
  passes from a clean boot.
  - Result: `regulator-always-on` grafted on `LDO_REG6`; loader-protect off
    path resets the panel state. Every earlier negative result was taken with
    the rail off. A truncated `orb cat` zImage cost one dark boot;
    `tools/pull-kernel.sh` hashes the copy.
  - Evidence: `docs/evidence/2026-09-03/snapshot-*`, `2026-09-03-gpio2-trace-v28-first-modeset.txt`,
    `camera/2026-09-03-v29-boot.jpg`; `kernel/patches/0003-*`.

- [x] 2026-09-03 — **Testing:** A camera closed the measurement loop and the
  panel was proved good.
  - Result: `tools/panel-camera/` (brightness and frame-to-frame motion,
    calibrated: off 33.7, on 104-118, floor 0.74-1.06); forcing the VOP to
    black left the panel white (deaf, not confused); flashing `boot-taq102-v14.img`
    back put the cube on the same panel, refuting the reviewer's hardware conclusion.
    Two real PHY defects found and kept (patch 0002, E4 common mode), neither
    the cause.
  - Evidence: commits `ee3f808`, `c3ca569`; `docs/evidence/2026-09-03-round2-findings.md`;
    `camera/v14-look.jpg`.

- [x] 2026-09-03 — **Kernel:** The 4.4.167 kernel builds and boots; the display
  comes up on it.
  - Result: `tools/build-kernel.sh` captures the seven host breakages; the
    non-boot was a malformed RSCE resource blob (`tools/make-resource.py`
    reproduces the stock image byte for byte); PHY `-19` then `-517` fixed by
    `CONFIG_PHY_ROCKCHIP_INNO_VIDEO_COMBO_PHY` as a module; the PWM pinctrl
    state must be named `active`; `tools/make-hybrid-dts.py` reproduces the
    tested blob. `/dev/dri/card0`, `LVDS-1` at 1024x600@56.14, glcube 54.8 FPS.
  - Evidence: commits `4281d04`, `9fd1b3f`, `cfd735a`, `5deda33`;
    `kernel/patches/0001-*`.

- [x] 2026-09-02 — **Infrastructure:** A logo is not a brick; host tools
  scripted.
  - Result: the tablet sat at the Denver logo because the BCB still said
    `boot-recovery` and `recovery` held a non-booting image; zeroing sector
    24608 brought it back. `tools/get-rkdeveloptool.sh`, `tools/get-mkbootimg.sh`,
    and a watcher polling `rkdeveloptool ld` once a second.
  - Evidence: commit `4281d04`; README "Two images, and how to get a console".

- [x] 2026-09-02 — **Backend:** SSH over Wi-Fi, key-only, keys on `/data`;
  reflashing through `reboot-loader` needs neither serial nor Android.
  - Evidence: commit `5df0c6a`; `br2-external/package/taq102-ssh`.

- [x] 2026-09-02 — **Frontend:** Gestures: arcball rotation with quaternion
  momentum, 1€ filter, two-finger zoom, drag and twist; four multitouch bugs
  from trusting slot state.
  - Evidence: commit `4a77073`; `src/arcball.c`, `src/oneeuro.c`; README "The
    gestures, and the four ways they were wrong".

- [x] 2026-09-02 — **Backend:** Wi-Fi up with the vendor `8723cs.ko` under the
  stock kernel; MAC pinned in `/data/wifi.mac` because `rk_vendor_read` fails.
  - Evidence: commit `85e5e13`; ping 1.1.1.1 in 20 ms, HTTP fetched;
    `blobs/8723cs-4.4.103.ko`.

- [x] 2026-09-02 — **Database:** Android erased; `userdata` (55 GB) is ext4 at
  `/data`, found by start sector 4867072, persistence verified over two reboots.
  - Result: all eleven partition backups checksum-verified first; 20 KB of
    `trust` had been zeroed by addressing a partition by number and was
    restored byte for byte (`PARTITION-NUMBERING-WARNING.md` on the archive).
  - Evidence: commits `28906c0`, `d0bc88f`.

- [x] 2026-09-02 — **Backend:** Mali-400 up with the r7p0 GBM blob (glibc,
  openssl for six RSA/BN symbols, no SONAME); `glcube` at 54.3 FPS on the
  56.14 Hz panel.
  - Evidence: commit `d34eb9e`; `br2-external/package/mali-utgard`.

- [x] 2026-09-02 — **Infrastructure:** Our own system boots from `boot` with
  `recovery` as the fallback; autostart at boot, backlight 255, rescue variant.
  - Result: BCB lives at `misc` + 16 KB (LBA 24608), not AOSP offset 0;
    nothing may boot Android between a recovery flash and its boot
    (`install-recovery.sh` restores stock). USB ACM console 2.68 s after
    kernel start.
  - Evidence: commits `3c7ca78`, `8cb318b`; README "Where the image lives now".

- [x] 2026-09-02 — **Frontend:** `particles`: a KMS particle field, 6000 of
  them at the panel's 56 Hz, tear-free; the musl 64-bit `time_t` made
  `struct input_event` 24 bytes against the kernel's 16 and ate every touch.
  - Evidence: commits "particles: ..." (2026-09-02); `src/particles.c`.

- [x] 2026-09-02 — **Infrastructure:** The image builds: Buildroot 2026.02.3,
  headers pinned to 4.4, Ubuntu 26.04's `uutils` and C23's `constexpr` worked
  around, cross-built in the OrbStack machine `taq102`.
  - Evidence: first three commits; `br2-external/configs/taq102_defconfig`.

- [x] 2026-09-01 — **Documentation:** Hardware identified and backed up; route
  settled.
  - Result: RK3126C on a BND-RK3126C-D708 board, Android 8.1, kernel 4.4.103,
    GSL3673, RK816, RTL8723CS; full eMMC image and 16 partition dumps,
    sha256-verified; GSL3673 firmware extracted from the stock kernel. Route:
    keep the stock boot chain, replace only the recovery ramdisk with a
    Buildroot userspace (the reviewer, two rounds).
  - Evidence: `/Volumes/Datos4TB2/denver-taq102/SHA256SUMS.txt` and
    `research/FINDINGS.md`; `~/p/brain/personal/denver-taq102-tablet.md`.

- [-] 2026-09-05 — **Kernel:** "The serial clock must be 7x the pixel clock"
  (350 MHz, prediv 12).
  - Resolution: measured with LDO6 off; superseded by the vendor's 336 MHz
    pair, which drives the panel with a hundredfold less jitter.

- [-] 2026-09-03 — **Kernel:** Hardware fault in the flex, connector or TCON
  (the reviewer round 2).
  - Resolution: refuted by the v14 control run on the same panel; the cause
    was LDO6.

- [-] 2026-09-03 — **Kernel:** The rebuilt PHY module "will not load" (`invalid
  module format`).
  - Resolution: a zero-byte file on the tablet; every copy now checks size and hash.

- [-] 2026-09-03 — **Kernel:** Capture U-Boot's live PHY registers before the
  kernel reprograms them.
  - Resolution: with no driver claiming the block it stays clock-gated and
    reads as zeroes; superseded by instrumenting the running kernel.

- [-] 2026-09-03 — **Kernel:** "The own-built 4.4.167 kernel never boots."
  - Resolution: it always did; the non-boot was the malformed resource blob and
    the missing console.

- [-] 2026-09-02 — **Infrastructure:** An SD rescue card (idbloader + miniloader
  + our system) to recover the dark tablet.
  - Resolution: superseded by loader mode plus the BCB at LBA 24608, and by
    `reboot-loader` from the running system; the card was never finished.

- [-] 2026-09-02 — **Infrastructure:** The volume-button combination boots
  `recovery`.
  - Resolution: the tablet has two buttons; the combination reaches loader
    mode, the single button trips the autostart hatch. The paths are software.

- [-] 2026-09-01 — **Infrastructure:** Replace U-Boot, and mainline first.
  - Resolution: never needed; the stock boot chain stays and the recovery
    ramdisk is ours. Mainline is the later track (see `TODO.md`).

- [-] 2026-09-01 — **Documentation:** the reviewer round 3 ("console without UART, what
  must be captured now").
  - Resolution: never answered (model at capacity); answered in practice by the
    USB ACM gadget console the next day.
