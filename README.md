# TAQ-102

Turning a Denver TAQ-102 tablet — Rockchip RK3126C, 1 GB RAM, LVDS 1024×600 —
into a Linux appliance that boots straight into one application.

Two patches that came out of this port are applied to the rtw88 maintainer
tree; see "Upstream" at the end of this file. The reasoning behind every
decision here, in the order it was learned, is in `docs/journal.md`.

This repo holds the Buildroot `br2-external` tree for the userspace, and since
2026-09-03 the scripts, patches and device tree that build our own 4.4.167
kernel for it (see "Building the kernel" and everything after "The display").
The first milestone ran under the stock vendor kernel 4.4.103, which already
drove the panel, the touch controller, the PMIC, KMS and the Mali-400 on this
board; the stock-kernel rescue image is still what `recovery` holds.

## Building

Buildroot needs a Linux host; macOS is not supported. The build runs in an
OrbStack machine, with the source tree read from the Mac and the output on the
machine's own disk.

```sh
orb create ubuntu taq102          # once
orb -m taq102 -u root bash -lc '
  apt-get update
  apt-get install -y build-essential file bc bzip2 cpio git rsync unzip wget \
                     perl python3 patch tar gzip libncurses-dev flex bison \
                     texinfo gawk ca-certificates
  update-alternatives --install /usr/bin/install install /usr/bin/gnuinstall 100
  git clone https://gitlab.com/buildroot.org/buildroot.git /work/buildroot
  cd /work/buildroot && git checkout 2026.02.3
'

orb -m taq102 -u root bash -lc '
  cd /work/buildroot
  export BR2_EXTERNAL=$HOME/taq102/br2-external
  make O=/work/output taq102_defconfig
  make O=/work/output -j8
'
```

Output is `/work/output/images/rootfs.cpio.gz`, about 1 MB against a 64 MB
recovery partition.

## Three things that will bite a fresh host

Each cost a failed build on 2026-09-02, and each is fixed in the tree rather
than in someone's shell history.

**Ubuntu 26.04 ships uutils, not GNU coreutils.** Buildroot refuses its
`install` outright, citing
[uutils/coreutils#12166](https://github.com/uutils/coreutils/issues/12166). The
`update-alternatives` line above is Buildroot's own suggested fix and is not
optional.

**C23 made `constexpr` a keyword.** Linux 4.4's `scripts/unifdef.c` uses it as a
variable name, and GCC 15 defaults to `gnu23`, so the headers package fails to
configure. `patches/linux-headers/0001-*.patch` renames it. This is the price of
pinning headers to the version the device actually runs, and it is worth paying:
`libdrm` built against headers describing ioctls a 4.4 kernel does not have is
exactly the mismatch that fails silently at runtime.

**Pinning the header version is not enough; the series must be declared too.**
`BR2_DEFAULT_KERNEL_VERSION="4.4.302"` alone leaves
`BR2_TOOLCHAIN_HEADERS_AT_LEAST` at its `2.6` default, and the strict check then
refuses the build with *"expected 2.6.x, got 4.4.x"*. Both
`BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_4_4=y` and the version string are needed.

## What the stock kernel forces on this image

Read from `/proc/config.gz`, recovered from the running tablet while Android was
still intact:

- **gzip is the only initramfs compression that works.** `CONFIG_RD_GZIP=y`;
  `RD_LZO`, `RD_LZ4` and `RD_XZ` are all unset.
- **`/init` must mount devtmpfs itself.** `CONFIG_DEVTMPFS=y` but
  `CONFIG_DEVTMPFS_MOUNT` is not set.
- **There is no console but the USB gadget.** `CONFIG_VT` is unset and there is
  no framebuffer console, so the kernel cannot print to the tablet's own panel;
  the physical UART2 pads have not been located. `CONFIG_USB_F_ACM=y` and
  `CONFIG_USB_CONFIGFS_ACM=y`, with a UDC at `10180000.usb`, are what make a
  console possible at all — which is why `/init` raises the ACM gadget before
  anything that can fail, and reports either outcome on `/dev/kmsg`.
- **No overlayfs, no squashfs.** A writable layer has to be tmpfs or ext4.

## The GPU

The stock kernel already carries the `mali-utgard` kernel driver for the
Mali-400 MP2 built in (`CONFIG_MALI400=y`), loads it at 2.64 s and exposes it as
`/dev/mali`. Only the user-space half was missing, and it is a blob: **r7p0**,
the version Android on this tablet reports in `ro.hardware.egl`
(`r7p0-00rel1-5-25`), taken from Rockchip's `libmali` mirror in its **GBM**
flavour, since there is no X11 and no Wayland here.

Measured on the device, 2026-09-02:

```
# glcube
gbm backend: drm
EGL 1.4 ARM
GL_RENDERER: Mali-400 MP
GL_VERSION: OpenGL ES 2.0
KMS up: 1024x600@56 on connector 55
54.3 FPS
```

54.3 FPS against a 56.14 Hz panel is the page flip waiting for vblank, with the
GPU still at its lowest 200 MHz devfreq step.

Three things the blob forces, each of which cost a build:

- **glibc, not musl.** The blob is `arm-linux-gnueabihf`, needs `GLIBC_2.4`
  symbols and links `libpthread.so.0`, `librt.so.1` and `libdl.so.2`. That is
  why the toolchain in `taq102_defconfig` changed, and why `BR2_ARM_EABIHF` is
  now explicit.
- **It leaves six OpenSSL symbols undefined** — `BN_bin2bn`, `BN_new`,
  `BN_set_word`, `RSA_new`, `RSA_public_decrypt`, `RSA_size` — because on
  Android they came from the process's own libcrypto. Nothing resolves them
  here, so `openssl` is a dependency and `patchelf --add-needed libcrypto.so.3`
  is part of the install.
- **It carries no SONAME at all**, so `patchelf --set-soname libmali.so.1`
  comes first, before the symlinks everything else links against.

Buildroot's own `rockchip-mali` package installs the Bifrost G31 blob and cannot
be pointed at another GPU, which is why `package/mali-utgard` exists. Its
`kmscube` package is no use either: it requires `gbm_bo_get_modifier`, which a
2016 Utgard GBM does not have. `glcube` in `src/` is the replacement — EGL/GLES2
on a GBM surface, `drmModeSetCrtc` once and `drmModePageFlip` after that, with
the touchscreen spinning the cube.

## Building the kernel

`tools/build-kernel.sh` builds the vendor 4.4.167 tree
(`54shady/qop_kernel`) and this board's DTB, inside the same VM, from a
`.config` given as `CONFIG=`. Run it there, never on macOS.

The script exists because seven separate things in that tree or its host break
a modern build, and each one fails as something else: `sudo -E` is ignored so
the architecture silently becomes the VM's own arm64; `scripts/` wants a
`python` on PATH; `scripts/dtc` needs `-fcommon`; **`scripts/gcc-wrapper.py` is
Rockchip's own wrapper that deletes the object and aborts on any warning
outside a GCC 6 whitelist**, so `error, forbidden warning:` is not a compiler
error and is bypassed by overriding `CC` on the make line; newer GCC warnings
are demoted with `KCFLAGS`; binutils >= 2.36 rejects `.section "...", #alloc`
in favour of quoted flags, at 33 sites in `arch/arm`; and
`drivers/media/i2c/sc031gs.c` calls a symbol this tree does not define, so the
driver is dropped from its Makefile rather than from `.config`, which
`default y` would undo on the next `olddefconfig`.

## Host tools

Neither is in Homebrew and both are fetched into `tools/vendor/`, which is not
tracked. `tools/get-rkdeveloptool.sh` builds rkdeveloptool 1.32 with the one
warning Clang makes fatal demoted; `tools/get-mkbootimg.sh` pulls AOSP's
`mkbootimg.py` plus the `gki` module it imports unconditionally.

## Flashing

`tools/make-recovery.sh` packs `rootfs.cpio.gz` into a recovery image with the
stock kernel and `second` blob and the offsets read from the stock image;
`tools/flash-recovery.sh` writes it and sets the bootloader control block.

Both take LBAs in **parameter coordinates**, which is what `rkdeveloptool`
speaks: recovery at 196608, the BCB at 24608. Addressed from inside a running
system on the raw device those are **8192 sectors higher** — recovery at 204800,
the BCB at 32800 — because the parameter block's offsets are relative to the end
of the 4 MB reserved region. Confirmed on the device on 2026-09-02 by reading
`boot-recovery` out of sector 32800 and ARM code out of 24608, which is `trust`.

Loader mode no longer requires Android. `/usr/sbin/reboot-loader` calls
`reboot(LINUX_REBOOT_CMD_RESTART2, "loader")`, which BusyBox's `reboot` applet
cannot do, and the device comes back as `2207:310d` for `rkdeveloptool`. This
matters because booting Android restores the stock recovery partition from
`recovery-from-boot.p` and silently undoes a flash.

### Mainline desk image (v89)

The desk itself is [spectalive/dmxdesk](https://github.com/spectalive/dmxdesk),
split out of this repository on 2026-09-26 with its history. Its tag is pinned
once, as `DMXDESK_VERSION` in `br2-external/package/dmxdesk/dmxdesk.mk`;
`tools/get-dmxdesk.sh` checks that tag out into `tools/vendor/dmxdesk/` for the
scripts below, and `DMXDESK_DIR` points them at a local checkout instead.

`tools/make-desk-ramdisk.sh <base.cpio.gz> <out.cpio.gz>` adds the desk to an
existing mainline ramdisk. It runs `tools/build-dmxdesk.sh` if `output/dmxdesk`
is missing; explicitly rebuild first to refresh an existing binary. It preserves
the original cpio bytes, appends a root-owned `newc` overlay, and compresses both
archives in one gzip stream. The diagnostic `/init`, modules, fonts and all
unrelated contents and modes survive. Existing output files are refused.

The overlay installs `/usr/bin/dmxdesk`, `/usr/bin/taq102-desk`, the updated
`taq102-app`, `/usr/share/dmxdesk/vibra.desk.json` and a `VERSION` line with the
workspace SHA256, the dmxdesk checkout's HEAD, dirty state and map-file SHA256. The launcher
waits for `silead_ts` and `rk805 pwrkey` by name; the master remains in
`/data/desk.conf`, and output remains in `/data/log/taq102-app.log`.

This is the v89 recipe; use unused output filenames for another build. The
`second-v88-logo-palette` file was extracted from v88's Android boot header.

```sh
gate=/Volumes/Datos4TB2/denver-taq102/gate3-build
tools/build-dmxdesk.sh
tools/make-desk-ramdisk.sh "$gate/rootfs-v87-sleep-logo.cpio.gz" "$gate/rootfs-v89-desk.cpio.gz"
KERNEL="$gate/zImage-7.3.0-rc2-v86-pci-only-0301" \
SECOND="$gate/second-v88-logo-palette" MKBOOTIMG=tools/vendor/mkbootimg.py \
sh tools/make-recovery.sh "$gate/rootfs-v89-desk.cpio.gz" "$gate/recovery-taq102-v89-desk.img"
```

The next full mainline Buildroot build selects the `dmxdesk` package, which
downloads the pinned dmxdesk release and cJSON 1.7.19 (both checked against
`dmxdesk.hash`). It and `tools/build-dmxdesk.sh` compile the same
`src/dmxdesk.sources` with the same flags. To validate packaging without
rebuilding the rootfs, enable `BR2_PACKAGE_DMXDESK` in the existing
configuration, run `olddefconfig`, then:

```sh
orb -m taq102 -u root bash -lc 'cd /work/buildroot && make O=/work/output-mainline dmxdesk'
```

`taq102-app` prefers the executable desk launcher, honors `TAQ102_APP`, and
switches to glcube after five desk exits. The cube then has its existing
five-attempt budget; the desk is not retried during that boot. Vendor images
without the package keep the previous cube behavior. Both rescue paths remain.
`python3 tests/boot/desk_boot_test.py` checks these paths and input discovery
with local fakes; it does not substitute for a hardware boot test.

**Owner acceptance remains:** start the loader watcher on the Mac, then enter
loader mode from the running tablet with `devmem 0x100a0038 32 0x5242C301; reboot`.
No flashing was performed while preparing v89.

```sh
tools/loader-watch.sh recovery /Volumes/Datos4TB2/denver-taq102/gate3-build/recovery-taq102-v89-desk.img
```

Verify the desk after a cold boot. Kill `dmxdesk` once per restart, five times:
the first four exits should restart it, and the fifth should leave glcube with
its control centre available. Check the log and SSH access, then cold boot
again to confirm the desk is selected anew.

## Where the image lives now

Since 2026-09-02 the same image is written to **both** `boot` and `recovery`,
and the bootloader control block is zeroed, so the ordinary power-on path runs
our system with no BCB involved and `recovery` is the fallback. Android no
longer boots — its ramdisk is gone — which also retires the
`install-recovery.sh` trap for good. The stock `boot.img` is backed up in the
archive beside every other partition.

| Partition | Parameter LBA (`rkdeveloptool`) | Raw device LBA (`/dev/mmcblk1`) |
| --- | --- | --- |
| boot | 131072 | 139264 |
| recovery | 196608 | 204800 |
| BCB (`misc` + 16 KB) | 24608 | 32800 |

The BCB can be written from inside the running system, which is the cheap way to
reach the other image: `dd` `boot-recovery` to raw sector 32800 and reboot, then
zero those 8 sectors to come back. Both images carry the same build id today, so
nothing distinguishes them once booted; give the rescue one its own id when that
matters.

## Two images, and how to get a console

`boot` carries the appliance and `recovery` the rescue variant. They come from
one build: `tools/make-rescue-ramdisk.sh` appends a two-file archive
(`/etc/taq102-no-autostart`, `/etc/taq102-variant`) to the **uncompressed**
`rootfs.cpio` and compresses the pair once. Appending a second gzip *stream* to
a finished `rootfs.cpio.gz` does not work on this 4.4 kernel — it unpacks the
first member and ignores the rest, which looks exactly like the files never
having been added.

`/init` raises the backlight to `max_brightness` before anything draws: the
device tree default is 128 of 255, and a half-lit panel reads as bad colour
rather than as half brightness. Then inittab's `::once:` runs
`/usr/bin/taq102-app`, which prefers the installed desk launcher and otherwise starts `glcube`; each
application has five attempts, with desk exhaustion falling back to the cube.

Three ways to keep the screen for yourself: the rescue image, holding **Vol−**
while it boots (`adc-keys` reports `KEY_VOLUMEDOWN` and `KEY_BACK` on `event2`;
this board has no `KEY_VOLUMEUP`), or `killall glcube` from the serial console,
which is always there either way.

## Persistent storage, and why not by partition number

Everything in this image lives in RAM and dies at reboot. The last partition —
Android's `userdata`, 55 GB at LBA 4867072 — is where anything durable goes, and
`/init` mounts it at `/data` **only if it already carries ext4**; creating the
filesystem is a deliberate `mkfs.ext4`, never something a boot does. The kernel
has `CONFIG_EXT4_FS=y` already, so only `e2fsprogs` had to be added.

It finds that partition by **start sector**, not by number, and the reason is
sharper than the Android-versus-us mismatch already on record: the same kernel
and the same image listed **17 partitions starting at LBA 8192 on one boot and
16 starting at 16384 on the next**, so `userdata` was `p17` once and `p16`
immediately after. A partition number is not a stable name on this device.

## The two buttons

There are exactly two: power, and one other. The device tree declares two ADC
keys — "Volume Up" at 0 V reporting `KEY_BACK` (158) and "Volume Down" at 1.65 V
reporting `KEY_VOLUMEDOWN` (114) — and one of them does not exist in the
plastic, so `taq102-app` accepts either code for its skip-autostart hatch.

Holding that button at power-on makes stock U-Boot boot `recovery`, which is our
rescue image. U-Boot also has a `fastboot key pressed.` path next to
`recovery key pressed.` that would reach its `rkusb` gadget — loader mode
without a host — but with only one non-power button there is nothing to press
for it. The way into loader mode is therefore: button → rescue image →
`reboot-loader`.

## Android is gone

2026-09-02, after verifying all eleven partition backups byte-for-byte against
`SHA256SUMS.txt`:

- `userdata` (55 GB, LBA 4867072) carries `mkfs.ext4 -F -m 0 -L taq102-data` and
  mounts at `/data`. 54.1 GB free, and a file written before a reboot reads back
  after two.
- The first 1 MB of `cache`, `system`, `metadata`, `vendor` and `oem` is zeroed,
  so nothing can mount or resurrect them.
- Untouched, because the boot chain and the recovery path live there:
  `idbloader`, `uboot`, `trust`, `misc`, `resource`, `kernel`, `boot`,
  `recovery`, `backup`, `security`, `frp`.

Restoring Android means writing `partitions/*.img` back from the host in loader
mode. There is no path back through the device itself, which is the point.

## Wi-Fi

The RTL8723CS works, on the stock kernel, with the vendor's own module:
`blobs/8723cs.ko` straight off this device. That is the payoff for not
rebuilding the kernel — `CONFIG_MODVERSIONS=y` means a rebuilt kernel must
reproduce the vermagic *and* every symbol CRC, while under the stock kernel both
match by construction. `cfg80211` and `mac80211` are already built in.

`/usr/sbin/taq102-wifi up` loads it, pins the MAC, associates and runs `udhcpc`;
inittab does that before starting the application. Measured: association to a
WPA2 network, `192.168.1.77`, 20 ms to 1.1.1.1, HTTP fetch, and reachable from
another machine on the LAN.

Two things live on `/data`, since nothing else survives a reboot:

    /data/wifi.conf   what `wpa_passphrase <ssid> <psk>` writes
    /data/wifi.mac    the MAC to keep

The MAC file is not a nicety. `rk_vendor_read` fails on this board — there is no
MAC in vendor storage — so the driver assigns a **random one on every boot**, and
without pinning it the tablet appears as a new device and takes a new DHCP lease
each time. The script saves the first one it is given and sets it thereafter.

**A local package does not reinstall itself.** Editing
`package/taq102-wifi/taq102-wifi` and running `make` changed nothing on the
target: the package's stamp was already there, so the install step never re-ran,
and the tablet kept the old script while the build looked clean. Use
`make taq102-wifi-reinstall` after editing a file a package installs from its
own directory. For a package that compiles sources (`glcube`, `rescue-screen`,
`particles`), `make <pkg>-rebuild` does not re-sync the source either; only
`make <pkg>-dirclean` followed by `make <pkg>` does. And the mount the VM
reads the tree through has served stale and truncated files: run
`tools/vm-hash-check.sh` before a build that matters.

## The gestures, and the four ways they were wrong

`glcube` rotates with one finger, and with two it zooms, drags and twists at
once. Getting there took four bugs, and every one of them was the same mistake:
trusting a piece of state that was not mine to assume.

- **The current slot at open.** `ABS_MT_SLOT` is stream state, emitted only when
  it changes, so a reader that opens the device mid-stream does not know which
  contact the next position belongs to. Assuming zero parked a phantom finger in
  slot 0 that never lifted — this driver never selects slot 0, so it never sends
  that slot a tracking id of −1 — and every pinch measured against a frozen
  point. `EVIOCGMTSLOTS` asks the kernel for the truth at open; before the first
  slot event arrives, positions are dropped rather than guessed.
- **What makes a slot active.** A tracking id does, and nothing else. Treating a
  position as evidence of a finger is what made the phantom stick.
- **The pinch anchor across a dropout.** This controller drops a contact for a
  frame or two mid-gesture — measured: the trace goes 2 slots, 1, 2, with both
  fingers still down. Ending the pinch on the first lone frame meant re-anchoring
  on the next at a new separation, and the cube jumped. A pinch now survives four
  such frames, and will not anchor on contacts less than 40 px apart, which are
  one finger being split in two.
- **Filter state on a reused slot.** The 1€ filter keeps position and velocity
  per slot; slots get reused. Without a reset per contact, a new finger starts
  where the *previous* one in that slot ended and slides to where it really is
  over the tenth of a second the adaptive cutoff needs to notice the jump — so
  closing two fingers read as separating, and the cube grew while being pinched
  smaller.

Clamping the zoom also has to re-anchor, or holding against the limit integrates
a zoom that cannot happen and the fingers must give all of it back before
anything moves.

The rotation itself is a **trackball** (Shoemake's ARCBALL, Graphics Gems IV
1992, with Holroyd's hyperbolic sheet outside the sphere) in `src/arcball.c`: a
drag rolls a sphere behind the screen, and the rotation is the great-circle arc
between where the finger was and where it is. There is no gain constant to tune,
the grabbed point stays under the finger at any speed, and dragging near the rim
twists rather than tumbles. Orientation is a quaternion, so there is no gimbal
lock and momentum keeps the axis of the spin while bleeding off its angle.

`src/oneeuro.c` is the 1€ filter (Casiez, Roussel and Vogel, CHI 2012) on each
contact's coordinates: heavy smoothing when a finger is nearly still, where
jitter shows and lag does not, and none when it moves, where lag shows and
jitter does not. A plain low-pass would trade the jitter for lag, which is worse.

Two fingers give three independent measurements and each drives exactly one
thing — separation is zoom, midpoint is the drag, the angle of the line between
them is the twist — which is what lets one movement do all three. The twist is
unwrapped across ±π, or one `atan2` crossing would spin the cube half a turn in
a frame.

`GLCUBE_TRACE=1` makes it print the slots it sees, their filtered positions, the
camera distance and the pinch anchor once a second. Every bug above was found
with that and a capture from `evtest`, not by guessing.

## SSH

`taq102-ssh` starts dropbear with everything that must outlive a reflash on
`/data`:

    /data/ssh/host_rsa, /data/ssh/host_ed25519   this machine's identity
    /data/ssh/authorized_keys                    who may log in

Host keys in `/etc` would be regenerated every boot, since the root filesystem
is an initramfs, and every login would then be a changed-host-key warning that
was right. Password authentication is off (`-s -g`): root has no password on
this image, and giving one to a device on the LAN is worse than requiring a key.

    ssh -i ~/.ssh/taq102 root@<address>

Loader mode is now reachable over the network too — `reboot-loader` over SSH,
then `rkdeveloptool` — so a reflash needs neither the serial cable nor Android.

## The Wi-Fi does not always come up

Measured on 2026-09-02, on a boot no different from the ones that work:

    RTW: ERROR sdio_deinit: sdio_disable_func(-5)
    rtl8723cs: probe of mmc2:0001:1 failed with error -110

The vendor driver loses a race with its own power-up sequence, unbinds itself,
and there is no `wlan0`. `taq102-wifi` retries once through the BSP's
`/sys/class/rkwifi` power and card-detect nodes, which costs nothing to attempt.

**What does not work is worth more than what does.** Once the chip has failed
this way, reloading the module does not recover it, the full BSP power sequence
does not recover it, and a warm reboot does not either — none of them takes
power off the RTL8723CS. Only a real power-off does. Trying harder in software
made it worse: after several reload attempts the SDIO card stopped being
detected at all. The script now says so on `/dev/kmsg` rather than failing
silently.

## Status

This section is the state on 2026-09-02, kept because the sections that follow
build on it. The current state is at the end of this file ("The shimmer was
the PHY's own PLL") and in `TODO.md`: the appliance runs on our own 4.4.167
kernel from `boot`, `recovery` holds a stock-kernel rescue, and the panel is
steady at the vendor's PHY PLL.

On 2026-09-02 the tablet booted this image under the stock kernel, drove the
panel through DRM, read multitouch, and rendered with the GPU. `particles` is
the CPU application, `glcube` the GPU one; `glcube` spins with one finger and
zooms with two, tracking multitouch slots directly from the GSL3673's
protocol-B event stream.


## Repository layout

    br2-external/       Buildroot external tree: packages, defconfigs, rootfs overlays
    src/                the appliance and the control centre, in C; the files it
                        shares with the desk are copies from spectalive/dmxdesk,
                        listed with their tag in src/dmxdesk-shared.txt
    tests/              host tests; run with tools/test-control-centre-host.sh
    tools/              build, flash, kernel and diagnostic scripts
    kernel/             device tree, vendor 4.4.167 patches, mainline patches
    blobs/              vendor binaries, not redistributed; see blobs/README.md
    docs/journal.md     what was measured and why, in the order it was learned
    docs/evidence/      logs, captures and photographs behind the claims
    docs/research/      the route findings that set the direction

## Tests

The host suite runs with AddressSanitizer and UndefinedBehaviorSanitizer; the
boot test drives the launchers with local fakes:

```sh
tools/test-control-centre-host.sh  # needs stb_truetype.h; STB_DIR to point at it
python3 tests/boot/desk_boot_test.py
```

The desk's own tests live with it, in spectalive/dmxdesk.

## Upstream

Two patches written for this device are applied to `rtw-next`, the rtw88
maintainer branch that feeds wireless-next. They fix an RTL8723CS that came
back from a warm reboot with the WLAN MAC unpowered while the SDIO function
still enumerated: the card-disable to card-emulation transition never withdrew
the SDIO suspend request on local register 0x86.

    43f6347e8bbe  wifi: rtw88: 8703b: complete the card-disable to card-emulation transition
    73e3b1c94c7d  wifi: rtw88: 8703b: stop the PCIe DMA write reaching SDIO

Both carry `Acked-by: Ping-Ke Shih <pkshih@realtek.com>`. The review thread is
at
<https://lore.kernel.org/linux-wireless/178914109042.64584.1681714272474754544@cristiandeluxe.dev/>.
They are in the maintainer's tree, not yet in Linus's.

## Firmware and vendor blobs

This repository redistributes no vendor binaries. The Realtek driver taken from
the stock Android system and the Silead touchscreen firmware are both extracted
from a device you own; `blobs/README.md` says how, and what each one has to
match. The build reads them from paths that `.gitignore` covers, so a checkout
is complete for everything except those files.

## Licence

GPL-2.0-or-later, in `LICENSE`. The repository carries kernel patches and
device trees derived from Linux, so the whole tree is distributed on those
terms.
