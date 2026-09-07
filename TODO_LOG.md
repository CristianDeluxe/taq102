# TODO Log

> Searchable record of closed project work. Active work lives in `TODO.md`.

## 2026

### 2026-09

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
    later (the 2026-09-03 hang is gone). Images archived in
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
