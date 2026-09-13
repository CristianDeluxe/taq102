# TAQ-102

Turning a Denver TAQ-102 tablet — Rockchip RK3126C, 1 GB RAM, LVDS 1024×600 —
into a Linux appliance that boots straight into one application.

Hardware notes, the recovered stock firmware and the gate-by-gate plan live
outside this repo: `~/p/brain/personal/denver-taq102-tablet.md`, and the archive
with every image and checksum is `/Volumes/Datos4TB2/denver-taq102/`.

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
  export BR2_EXTERNAL=/Users/<you>/p/taq102/br2-external
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
`/usr/bin/taq102-app`, which starts `glcube` and restarts it at most five times.

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

## The display, and what the boot images were really doing wrong

Three failures hid behind one symptom -- a tablet that sat at the Denver logo
and enumerated nothing -- and each one was mistaken for the previous one.

**The kernel was never the problem.** The own-built 4.4.167 boots, reaches
userspace and serves the USB console; `uname -r` says so. With no display, no
UART and no network, a system that boots and one that is dead look identical
from the host, which is how a working kernel spent a day being called broken.

**The resource image was malformed.** RSCE is 512-byte blocks: a header, one
index block per entry, then the payloads. A hand-built image carrying only
`rk-kernel.dtb` starts its payload at block 2, but the index copied from the
three-entry stock image still said block 4, and the header still claimed three
entries. U-Boot read the FDT 1 KB into the blob, found no magic and booted
nothing, silently. `tools/make-resource.py` rebuilds the image properly and is
verified by reproducing the stock one byte for byte.

**The LVDS binding changed between the two kernels.** The stock tree gives LVDS
its own registers at 0x20038000 beside a separate mipi-dphy; the 4.4.167 tree
has one shared video PHY there with LVDS as a GRF child pointing at it. So the
factory blob boots this kernel but leaves `failed to get phy: -19`. Grafting
only that change onto the stock tree moves the error to `-517`
(EPROBE_DEFER) -- and the reason nothing ever probed is that
`CONFIG_PHY_ROCKCHIP_INNO_VIDEO_COMBO_PHY` was not set.

With the PHY driver built in, the boot hangs the moment LVDS powers the PHY,
before fbdev registers, which is why even a framebuffer console shows nothing.
`kernel/patches/0001-video-combo-phy-clocks-and-pll.patch` enables
HCLK_VIO_H2P around the DSI-host accesses that path makes -- the dsi node lists
that clock beside PCLK_MIPI, and in LVDS mode no DSI driver is there to enable
it -- but the hang survives it, so the cause is still open.

## Why the panel never came up: a pinctrl state name

The display failure is a chain, and every link was invisible from outside.
`pwm-rockchip` in the 4.4.167 tree requires a pinctrl state literally named
**`active`**, and refuses to probe without one:

```c
pc->active_state = pinctrl_lookup_state(pc->pinctrl, "active");
if (IS_ERR(pc->active_state)) {
	dev_err(&pdev->dev, "No active pinctrl state\n");
	return PTR_ERR(pc->active_state);
}
```

The stock device tree, written for 4.4.103, says `pinctrl-names = "default"`.
So no PWM chip registers; with no PWM there is no backlight; `panel-simple`
defers waiting for the backlight -- 390 retries in one boot, which floods the
ring buffer and scrolls the PWM error out of it; the LVDS encoder defers on the
panel with `-517`; and `rockchip-drm` reports `master bind failed: -517` and
registers no device. Renaming the state on `pwm@20050000` is the whole fix, and
the vendor's own 4.4.167 `rk312x.dtsi` confirms it: same pin, same clock,
`pinctrl-names = "active"`.

**Building the PHY driver as a module is what made this readable.** Built in, it
hangs the boot before fbdev exists, so the machine says nothing at all. As a
module the system boots to a console, `insmod` returns 0, nothing hangs, and
`dmesg` names the failure. That difference is itself a finding: the hang is a
boot-time ordering problem, not the driver.

The lesson worth keeping: when a failure is silent, spend the next move on
making it speak rather than on another guess at the cause.

## The panel carries signal and no content: what the registers rule out

2026-09-03. The display path comes up on our own kernel and the panel still
shows nothing. This section records what was measured rather than what was
tried, because almost every cheap suspect is now eliminated by a register read
and re-guessing them costs another night.

**The working stock kernel is a readable reference, up to a point.** It has
`CONFIG_DEVMEM` unset, so its registers cannot be read at all -- but it mounts
debugfs, and `/sys/kernel/debug/dri/0/summary` plus `clk_summary` answer two
questions that were previously guesses:

|                  | stock 4.4.103, picture | ours 4.4.167, blank |
| ---------------- | ---------------------- | ------------------- |
| `dclk_vop`       | **49.5 MHz**, parent gpll 594 | **50.0 MHz**, parent cpll 400 |
| summary `real_clk` | 51200                | 50000               |
| `bus_format`     | 0x1009                 | 0x1012              |
| `output_mode`    | 0 (P888)               | 0 (P888)            |

So the panel that works is driven at 49.5 MHz, not at the 51.2 MHz its own
timing asks for, and neither kernel achieves 51.2. **The 7:1 rule is not
violated on our side**: the PHY is programmed 24/12*175 = 350 MHz against a VOP
at exactly 50 MHz. That eliminates serialiser slip, which had been the leading
theory.

**The VOP is configured correctly and is fetching real pixels.** Read with the
mode set and `glcube` running:

```
DSP_HTOTAL_HS_END 0x05860046   htotal 1414, hsync 70
DSP_HACT_ST_END   0x00E604E6   hact 230..1254
DSP_VTOTAL_VS_END 0x0285000A   vtotal 645, vsync 10
DSP_VACT_ST_END   0x00210279   vact 33..633
DSP_CTRL0         0x00000080   out_mode P888, dither_down off, dclk_pol 1
DSP_CTRL1         0x00000000   dsp_blank 0
SYS_CTRL          0x00000001   standby 0, win0 enabled
AXI_BUS_CTRL      0x1F100000   rgb_en 1, lvds_en 1
WIN0_YRGB_MST     0x00258000   with glcube running (0 when only fbcon is up)
```

`WIN0_YRGB_MST` is an IOVA, not a physical address: the VOP sits behind
`iommu@1010e300`, which reads `DTE_ADDR 0x64E1E000` and `STATUS 0x19` --
paging enabled, idle, replay buffer empty, **no page fault**. Framebuffer fetch
is therefore not the problem, which retires the whole "is it scanning out
anything" line of enquiry.

**Two real defects were found in the PHY's LVDS path, fixed, and did not fix
the panel.** MIPI mode powers the shared analog block up; LVDS mode never did.
Measured before the fix, with the display up:

```
analog reg00 = 0x01   common analog lanes [6:2] all DISABLED
analog reg01 = 0xE3   REG_LDOPD and REG_PLLPD both POWERED DOWN
```

while every LVDS-specific register was already correct -- mode enable, digital
enable, all five LVDS lanes on, dividers 12/175, and `GRF_LVDS_CON0 = 0x034A`
(P2S_EN, MODE_EN, MSBSEL, format 1 = JEIDA-24). `kernel/patches/0002-*` adds
the two writes MIPI mode makes, and afterwards the registers read `reg00 =
0x7D` and `reg01 = 0xE0` -- exactly the intended values. **The panel stayed
white.** The defect was real and is worth keeping; it was not the cause.

**Eliminated by sweeping the hardware live**, with `glcube` running and the
panel watched throughout: all four `GRF_LVDS_CON0` format codes (VESA/JEIDA,
24/18), both sample-clock directions, and all eight sample-clock phases. White
at every one of the fourteen settings.

**Eliminated by reading the CRU**: every `SOFTRST_CON0..8` reads zero, so the
PHY's `resets = <0x3 0x24>` is not being held asserted. The driver never calls
`reset_control_deassert()`, but nothing else is asserting it either.

**`PLL is not lock` is confirmed a false negative.** `DSI_PHY_STATUS` at
host+0xb0 was stepped through every value of `DSI_PHY_RSTZ` (host+0xa0) from
0x1 to 0xF and never changed from `0x00001FBC`. In LVDS mode that bit reports
on a block that is not in this path.

### What is left, and it is the strongest lead

The shared MIPI/LVDS **controller** at `0x10110000` -- the region `/proc/iomem`
calls `mipi_lvds_ctl` -- is essentially unconfigured. Scanning its first 256
bytes with the display up finds only a version register and reset defaults:

```
000=0x3132312A  034=0x00000001  074=0x00000015  0a4=0x00000003
0b0=0x00001FBC  0b4=0x00000001  0c4=0x001FFFFF  0c8=0x0003FFFF
```

The 4.4.167 combo-PHY driver touches exactly two registers in this block, both
in the DSI host page (`RSTZ` and `STATUS`), and programs nothing that would
format parallel pixels into LVDS channels. The stock 4.4.103 tree, by contrast,
gives LVDS its own node with its own registers and a driver for them.

That is consistent with every measurement above: VOP output correct, GRF mux
correct, PHY analog and PLL correct, panel receiving clock and reacting to
modesets -- and no payload, because the controller that builds the payload was
never told to. The next move is to read the stock rk312x LVDS transmitter
driver (`54shady/qop_kernel` carries one) for this block's layout, not to try
another setting.

That reading was wrong, and the vendor tree says so plainly. `rk312x.dtsi`
declares `0x10110000` as `compatible = "rockchip,rk3128-mipi-dsi"` with
`status = "disabled"`, and `lvds` as a separate node with no registers of its
own. There is no LVDS transmitter hiding in that block: in LVDS mode nothing
takes the DSI controller out of reset, which is also why its `PLL is not lock`
bit is a false negative. Stepping `DSI_PHY_RSTZ` through 0x4, 0x1, 0x3, 0x5,
0x7 and 0xF never moved `DSI_PHY_STATUS` off `0x00001FBC`.

Worth recording alongside it: grepping `&lvds` across every `rk3126*` and
`rk312x*` board file in the vendor tree returns exactly one hit, the disabled
node definition itself. No shipping board ever enabled this path, which is why
its analog-power bug sat latent -- and why "copy what the vendor does" cannot
resolve this, our copies of both drivers being byte-identical to theirs.

### A tool that did not do what it said

`tools/make-hybrid-dts.py` claimed to reproduce the tested blob byte for byte
and had stopped doing so. Its PWM graft matched `#pwm-cells = <0x3>;` while the
stock tree writes `<0x03>`, so `str.replace` substituted nothing and returned
happily -- the exact failure the resource-blob bug had already taught. The
grafts now locate the node and raise if the text they expect is absent, and the
regenerated tree once again decompiles identical to the blob under test.

## A camera closes the loop, and the panel is proved deaf

Every result above shares a weakness: the only instrument that could read the
panel was a human looking at it and typing back. That made each experiment cost
a round trip, so the experiments stayed few and the guesses stayed cheap --
exactly the wrong ratio. Pointing a webcam at the tablet fixed the ratio. One
`ffmpeg -f avfoundation` grab plus `signalstats` gives brightness and
frame-to-frame motion, and with `glcube` spinning, content on the panel moves
while a dead panel does not.

Calibrate the instrument before trusting it. Backlight off reads `YAVG=33.7`,
backlight on `YAVG=104..118`, and two frames of a static white panel differ by
`motion 0.74..1.06`. That noise floor is the whole measurement: anything at the
floor is a dead end, anything well above it is worth a photograph.

Photograph it, though, before believing it. Sweeping the VOP polarities turned
up `dclk_pol=1, pin_pol=1` at `motion=4.15`, four times the floor and
accompanied by a jump in `YHIGH`. It was the camera's auto-exposure. A re-measure
gave 0.76 and the photograph was white. A single number from an automated loop
is a lead, not a result.

### The decisive test: make the VOP emit black

`dsp_blank` (DSP_CTRL1 bit 24, latched with `REG_CFG_DONE`) makes the VOP emit
black in hardware, touching neither buffers nor PHY. A panel that decoded the
link would go black.

The panel did not change. It stayed white.

That single measurement reframes the problem. White is not a wrong picture, and
not a picture at all: it is the backlight shining through a panel that has no
valid signal to display. Every "the content is wrong" hypothesis -- bit order,
common-mode level, colour format -- is answering a question the panel never got
far enough to ask.

### Both sides measure correct

The VOP is scanning real frames. With `glcube` running, `WIN0_YRGB_MST`
alternates between `0x004B0000` and `0x00258000` -- a real page flip, not a
stuck pointer -- and the VOP interrupt advances 498 to 664 in three seconds,
about 55 fps against a 56 Hz mode.

The PHY holds the stock register state exactly. Disassembling the stock
`vmlinux.elf` yields the working driver's write sequence, and it revealed
something the register dumps could not: the stock driver **clears E1 bit 7 to
stop the LVDS digital block, writes its configuration, and sets the bit again**.
Ours never does that cycle, so it was writing fields that are sampled only when
the digital block leaves reset. Replaying the full stock sequence in the stock
order through that reset leaves E0=0x45, E1=0x92, E3=0x02, E4=0x80, E8=0xFC,
EB=0xF8, analog01=0xE0, GRF=0x034A -- byte for byte the state of the kernel that
drives this panel.

The panel stayed white.

### What the loop killed

Each of these was swept through the digital reset and measured against the noise
floor, and each came back at it:

| Candidate | How it died |
| --- | --- |
| E4 common-mode voltage | 0x80, 0x90, 0xA0, 0xB0 -- the whole VOCM range at stock swing |
| PHY-internal MSB select (E0 bit 0) | both values |
| PLL divisors | stock 2/28 (336 MHz) and ours 12/175 (350 MHz) |
| VOP polarities | all 16 combinations of `dclk_pol` x `pin_pol` |

The E4 write is worth its own note. It was found by disassembling the stock
kernel, it is documented in Rockchip's own display FAQ, our driver genuinely
never performs it, and it is a real defect that should be fixed. It is not this
bug. A correct-looking cause with a good provenance is still only a hypothesis
until the instrument answers.

### Two things that block the fast loop

A module rebuilt from the same tree will not load: `insmod` returns `invalid
module format` and the kernel logs nothing at all. Compared against the working
`/data/phy-fixed.ko` with readelf and objdump, the vermagic string, the
`__versions` table, the ELF flags and the ARM attributes are all identical. Any
driver-level experiment is blocked until that is understood, and it deserves
instrumenting rather than guessing.

`unbind` on `rockchip-drm` is not reversible: afterwards `bind` answers `No such
device`, and only a reboot brings the display back. The PHY module does not
autoload, so every reboot needs `insmod` of
`/lib/modules/4.4.167/extra/phy-rockchip-inno-video-combo-phy.ko` -- which is
the original, without the analog-power patch -- before there is anything to
measure.

### The harness

`tools/panel-camera/` holds the loop, one script per job: `tablet.sh` (a command
on the tablet over ssh), `snap.sh` (one webcam frame), `measure.sh` (brightness
and motion, with the calibration recorded in its header), `phy-write.sh`
(writes wrapped in the LVDS digital reset) and `vop-write.sh` (writes plus
`REG_CFG_DONE`). Verified with `./measure.sh repo-check`, which prints the noise
floor against the white panel.

### The control that should have been run first

the reviewer's round-2 conclusion was that the fault is physical -- a damaged LVDS flex
or a dead panel -- because a 45-second recording across a reboot never showed
U-Boot's DENVER logo. The reasoning was sound and the conclusion was wrong, and
the refutation was already in this session's own transcript: at 04:20 and 04:44
the same day, the stock 4.4.103 appliance had been running with the cube visible
on this panel.

Flashing `boot-taq102-v14.img` back to `boot` settled it in one boot. The image
was written and read back byte for byte
(`8eba528f9f4f8169f8ea244696868b4c2ac4f2003f99760dd4ee022e5ec4c96b` both ways),
and the camera then measured `YAVG=82.9 YHIGH=255 motion=6.96` against a white
panel's `116 / 0.78`. The photograph shows the cube turning on a blue field.

**The panel, the flex, the connector and the TCON are all good.** The fault is
in our LVDS path, and every measurement above still applies to it.

The lesson is about controls, not about review. Once the panel had refused every
software candidate, the cheapest remaining question was no longer "which
register is wrong" but "does the known-good image still work" -- and that
question had a stored answer costing one flash. A negative result invites a
hardware conclusion, and a hardware conclusion needs the control before it is
believed, not after.

### The golden reference, read from the working kernel

With v14 running and the panel showing content:

```
VOP [1010e000.vop]: ACTIVE          Connector: LVDS
overlay_mode[0] bus_format[1009] output_mode[0] color_space[0]
Display mode: 1024x600p56
clk[51200] real_clk[51200] type[8] flag[a]
H: 1024 1184 1254 1414      V: 600 612 622 645
dclk_vop 49500000 (gpll 594 MHz / 12)
```

Ours reports the identical mode and timings but `real_clk[50000]`, off cpll
(400 MHz / 8). The stock kernel has `CONFIG_DEVMEM` unset, so its PHY registers
cannot be read directly; debugfs is the only window, which is why the stock
write sequence had to come from disassembly.

## The panel had no power, and the kernel that worked was the one that failed to switch it off

2026-09-03, evening. Every register in the VOP, the PHY and the GRF had been
read, swept and matched to the stock kernel byte for byte, and the panel was
still white with the VOP forced to black. What no register dump covers is the
PMIC. `tools/panel-camera/snapshot.sh` captures what a kernel exposes without
`/dev/mem` -- regulators, the RK816 regmap, pinmux, gpio, clocks, power
domains, the DRM summary -- and diffing the two kernels' snapshots file by
file (`docs/evidence/2026-09-03/`) gave the answer in one line each:

|                            | stock 4.4.103, picture | ours 4.4.167, white |
| -------------------------- | ---------------------- | ------------------- |
| `regulator.12` (ldo6)      | `state=enabled`        | `state=disabled`    |
| RK816 reg 0x28, LDO_EN_REG2 | `0x73`                | `0xf1`              |

**LDO6 is the panel's 3.3 V.** Nothing in the device tree claims it and its
node says only `regulator-boot-on`, so the regulator core switches the unused
rail off at late init: ours logs `ldo6: disabling` and succeeds. The stock
kernel logs the same line and then `ldo6: couldn't disable: -1` -- a vendor
hack in its regulator core (the `+++++enter _regulator_do_disable` prints
around it) refuses, and that refusal is what kept this panel alive for the
tablet's whole life. "Copy the vendor" could never find it: both display
drivers were already identical, the difference sat in a third subsystem.

Measured, not argued: with our kernel up and `glcube` running,
`i2cset -f -y 2 0x1a 0x28 0x22` set the enable bit (readback `0x23`), and a
restart of `glcube` put the cube on the panel: `YAVG=82..87 motion=5.5..13.3`
against the v14 control's `82.9 / 6.96`, photograph in
`docs/evidence/2026-09-03/camera/`. `tools/make-hybrid-dts.py` now grafts
`regulator-always-on` onto `LDO_REG6`, which reproduces exactly the state the
stock kernel measures; v28 boots with `reg 0x28 = 0x73` and no `disabling`
line.

**Every earlier negative result was taken with the rail off**, so every one
of them is void, not wrong: the polarity sweeps, the PLL divisors, the
common-mode voltage, the panel-enable GPIO, the loader logo. The register
work was still worth doing -- it is what proved the fault was not on the link
-- but the lesson is sharper than "read more registers": when two kernels
behave differently on identical hardware, diff everything both of them
expose before reading anything either of them hides.

### The first modeset still leaves the panel enable low

v28 boots with the rail on and the panel is white again, and the enable GPIO
(`GPIO2_B4`, `gpio-76 enable`) reads `out lo` where the stock kernel reads
`in hi`. Raising it with `devmem` (bank `0x20084000`; `0x2007c000` is GPIO0,
which cost one wrong write) gives the picture at once. A 50 ms poll of the
GPIO2 direction and data registers (`docs/evidence/2026-09-03/*gpio2-trace*`)
shows the pin already output-low before the PHY module loads, before DRM
binds, and never moving through the modeset or the `glcube` start: 138
samples, no transitions.

So two things conspire. U-Boot parks the pin low because our hybrid tree no
longer carries the `lvds@20038000` node its own display code reads, so it
never brings the panel up. Then `rockchip_drm`'s `setup_initial_state()`
calls `loader_protect(on)` on the panel *before* knowing whether the loader
left a display running -- `panel_simple_loader_protect(on)` marks the panel
prepared and enabled without touching hardware -- and when the route fails
(`can't not find any loader display`) the off path is literally
`/* do nothing */`. The flags stay set, the first real `prepare()` returns
early, and the enable is never driven. Only a later unprepare/prepare cycle
(`glcube` exiting and restarting) ever raised it, which is why the second
modeset showed the picture and the first did not.
`kernel/patches/0003-*` makes the off path reset the state.

### v29 and a copy that was short

The v29 image with that patch left the tablet dark at U-Boot, with no USB
gadget and no Wi-Fi. Not the patch: the zImage inside the image is 6,946,816
bytes and the one the VM built is 7,868,368. `orb -m taq102 cat zImage >
file` cut the stream at a round 0x6A0000 without an error -- the second
silent short copy of the day, after the zero-byte module. `tools/pull-kernel.sh`
now copies through the shared `/Users` mount and hashes both sides, and the
rebuilt `recovery-taq102-v29-loaderprotect.img` carries the full kernel
(`047c0cba...`). U-Boot raises no USB at the logo and the volume button did
not reach `recovery` this time; both buttons at power-on brought loader mode
up after a few tries, `tools/flash-boot.sh` wrote and verified the image, and
**the fixed v29 shows the picture from a clean boot with nothing done by
hand**: `ldo6=enabled`, RK816 `0x28 = 0x73`, `gpio-76 enable out hi` after
the first modeset, and the camera at `YAVG 84..92, motion 5.7 / 8.5 / 20.5`
(`docs/evidence/2026-09-03/camera/2026-09-03-v29-boot.jpg`). The display path
on the own-built kernel is closed. Still by hand for now: `insmod` of the PHY
module after boot, which belongs in the image next.

## The appliance on our own kernel

2026-09-03, 21:10. `boot` now holds `recovery-taq102-v31-appliance.img`: the
4.4.167 kernel with patch 0003, the LDO6 resource image, and a ramdisk that
brings the display up by itself. Measured on the tablet from power-on, with
nothing sent over ssh:

```
[    4.370] taq102-display: loaded /lib/modules/4.4.167/extra/phy-rockchip-inno-video-combo-phy.ko
[    4.388] rockchip-vop 1010e000.vop: [drm:vop_crtc_enable] Update mode to 1024x600p56, type: 7
[    5.104] taq102-app: starting /usr/bin/glcube (attempt 1)
```

and the camera at `motion 21.2 / 11.5` fifty seconds later. Wi-Fi and ssh come
up as before.

What changed in the tree for that:

- **`package/taq102-display`** installs the combo PHY driver as a module
  (`blobs/phy-rockchip-inno-video-combo-phy-4.4.167.ko`, built from our tree
  with patch 0002 applied) and the script `/init` runs to load it before the
  backlight is raised. It stays a module on purpose: built in, the driver
  hangs the boot at the PHY's first power-on before any console exists, and
  loaded 4 s later the same code brings the display up in 20 ms. Under the
  stock kernel the script finds no module and does nothing.
- **`taq102-app` waits for `/dev/dri/card0`.** The deferred-probe workqueue
  finishes binding rockchip-drm after `insmod` has returned, and the first
  appliance boot started glcube 0.27 s too early, spending one of its five
  attempts on `Open card0: No such file or directory`.
- **Patch 0002 is confirmed harmless with the panel alive** (camera 8.5 / 7.8
  with it, 5.7 / 20.5 without). Worth knowing: the picture also comes up with
  the analog block reading `reg01 = 0xE3` and `E4 = 0xAA` -- the reset
  defaults -- so neither the analog power-up nor the common-mode write is
  needed by this panel. Both stay because they match the stock state.

The images are packed from the VM's `rootfs.cpio` copied through the shared
`/Users` mount and hash-checked on both sides, then `make-rescue-ramdisk.sh`
for the rescue variant and `make-recovery.sh` with `KERNEL=` and `SECOND=`
pointing at our own kernel and resource image.

`recovery` held v18 at this point, and the note that called it "the
stock-kernel rescue" was wrong: splitting the image shows its kernel is
`zImage-v16-fbcon`, our own 4.4.167 without the PHY driver -- a rescue with a
USB console and no picture. What replaced it is below.

### The button does not reach `recovery`

Measured 2026-09-03 21:20, from a clean power-off with the volume button held
from before power-on: U-Boot booted `boot` (v31, build `821c731`), not
`recovery`. What the button did was trip the appliance's own skip-autostart
hatch -- `taq102-app: KEY_BACK held at boot: not starting /usr/bin/glcube` --
and with nothing drawing, the framebuffer console showed the kernel log on the
panel (`docs/evidence/2026-09-03/camera/2026-09-03-recovery-button.jpg`).
Earlier today, from the dark U-Boot of the truncated v29, the same button did
nothing at all. So the sentence above about the button making U-Boot boot
`recovery` was never confirmed on this board: the ADC key that exists reports
`KEY_BACK`, and whatever U-Boot's recovery key is, it is not this one.

The ways into `recovery` are therefore two: `boot-recovery` written to the
BCB at raw sector 32800 from a running system, and loader mode -- both buttons
at power-on, or `reboot-loader` -- followed by a flash. Neither needs the
button alone, and both need a system that is already up or a host at the USB
cable. That is the real value of keeping a known-good kernel in `recovery`:
it is reached by software, not by a key U-Boot does not read.

Worth keeping from the same photograph: **our kernel has a visible console.**
`console=tty0` and the framebuffer console are in its command line and
config, so whenever nothing owns the screen the kernel log is on the panel.
The stock kernel, with `CONFIG_VT` unset, never had that.

### `recovery` now holds a stock-kernel rescue, and the round trip is measured

2026-09-03, 22:25. `recovery-taq102-v31-stockkernel-rescue.img` is the stock
4.4.103 kernel and resource image with the v31 rootfs in its rescue variant --
the same userspace as `boot`, no autostart -- so it is a fallback that
survives anything that breaks the 4.4.167 build. `taq102-wifi` picks the
4.4.103 module by `uname -r` and `taq102-display` finds no module and does
nothing, which is exactly what one image booting on either kernel means.

Written with `tools/flash-recovery.sh`, which now reads the span back and
compares SHA-256 before it touches the BCB, and then exercised end to end
without a button:

1. `flash-recovery.sh` sets the BCB to `boot-recovery` and resets; the tablet
   comes up on 4.4.103, build `821c731`, `variant=rescue`, ssh answering, the
   stock kernel logging its `ldo6: couldn't disable: -1` as ever. The panel is
   white: under the stock kernel nothing sets a mode until an application
   does, so a rescue with no app shows backlight and no signal. Starting
   `glcube` from ssh would paint it.
2. Zeroing the eight BCB sectors at raw 32800 from inside and rebooting
   brings `boot` back: v31, `glcube` at 4.98 s, camera `motion 17.3`.

**What the buttons do, measured today, so nobody spends another evening on
them.** With the tablet running, pressing anything does nothing U-Boot will
see. Holding power does not switch the tablet off while USB is plugged in:
the PMIC comes straight back up in charger mode (`androidboot.mode=charger`
on every boot). A `reboot` with both buttons held booted `boot` and did not
even trip the appliance's KEY_BACK hatch. The one time both buttons reached
loader mode was from the dark U-Boot of the truncated v29, after several
tries. So the paths into `recovery` and into the loader are software:
`boot-recovery` in the BCB, and `reboot-loader` -- and both need a system
that is up, which is the argument for a rescue kernel that is not the one
under development.

## The rescue image has a face

2026-09-03, 22:45. A rescue boot used to be a white panel, indistinguishable
from an appliance that failed to draw: under the stock kernel nothing sets a
mode until an application does. `rescue-screen` (`src/rescue-screen.c`,
`package/rescue-screen`) paints an amber screen with RESCUE MODE, the
kernel, the build id and the Wi-Fi address, refreshed as the address arrives,
through one DRM dumb buffer and a 3x5 bitmap font. `taq102-app` execs it
whenever autostart is off -- the rescue marker or the button hatch -- so the
same binary serves `recovery` (stock kernel) and a held-button boot of `boot`
(own kernel). `killall rescue-screen` frees the display.

Verified from clean boots on both kernels
(`docs/evidence/2026-09-03/camera/2026-09-03-v34-rescue-*.jpg`). Two things
the stock kernel taught on the way, each measured against the camera:

- **The stock VOP blends XRGB8888 as ARGB.** With 0x00 in the top byte the
  window is transparent and the panel shows a washed-out white with a ghost
  of the picture; with 0xFF the screen appears. `glcube` never hit it because
  GBM buffers carry 0xFF. `particles` has the same defect and is left as it
  is. The own kernel does not care either way.
- **The stock kernel's first modeset after boot is blank; the second shows
  the picture.** A fresh rescue boot was white with `rescue-screen` running
  and its `drmModeSetCrtc` returned 0; restarting the program painted the
  screen. `glcube` never sees it because it page flips immediately. The
  program now sets the mode, drops the CRTC and sets it again, once.

And one that is ours: two processes with a DRM device open and both setting
modes produce a torn, colour-shifted picture that looks like a stride or
bit-order fault. It was the supervisor relaunching `glcube` under a
`rescue-screen` started by hand. Stop `taq102-app` before taking the screen.

Images at this point: `boot` = `recovery-taq102-v34-appliance.img`,
`recovery` = `recovery-taq102-v34-stockkernel-rescue.img`, both from build
`20260903-204222-8fa9f47`, BCB zero.

## The rescue screen shows the battery and the Wi-Fi signal

2026-09-04, 01:10. Two more lines on the amber screen, refreshed every two
seconds: `BATTERY 0% 3.38V Charging -453MA` and `WIFI 192.168.1.57 -39DBM
Q82`, from `/sys/class/power_supply/battery` and `/proc/net/wireless`. Both
kernels expose them. `/proc/net/wireless` prints `100.  -37.` and `%f`
swallows the dot, so the fields are read as integers with a literal dot after
each -- the first build showed the address and no signal.

The battery line paid for itself the moment it existed. On a powered 12 V
USB hub with the backlight at 255 the tablet said `Charging` while draining
300 to 450 mA, and ran the battery from 12% to 0% at 3.36 V over the evening. At
brightness 40 it charges at 170 mA. `/init` raises the backlight to the
maximum because a half-lit panel read as bad colour; on USB power that
choice empties the battery, and the number, not the status word, is what
says so.

**The flicker I sees is not yet measured.** Both kernels, both
images; the webcam sees none of it at its 10 to 24 fps and long exposure
(four recordings of 20 s, static and moving, every band flat to 0.3 of
brightness), DDR frequency scaling made exactly one transition since boot,
GPU frequency none, and the backlight PWM runs at 100% duty. With the
static rescue screen I sees no flicker; with `glcube` he does. The
open candidates are the battery at 3.4 V sagging under GPU load, and the
page-flip path itself. The next measurement is `glcube` on a charged
battery, then `particles` (CPU load, page flips, no GPU) against the same
eyes.

Images: `boot` = `recovery-taq102-v35-appliance.img`, `recovery` =
`recovery-taq102-v35-stockkernel-rescue.img`, build `9dc924d`, BCB zero.

## The cube stops in five seconds, and the Mac's USB port cannot feed the tablet

2026-09-04, 01:55. Two findings from one evening of leaving the appliance
alone.

**The cube was still.** `glcube` at 54.8 FPS, the VOP flipping 55 times a
second, no touch events, and the cube dead: the start-up spin is fed through
the same coast as touch momentum, which bleeds 1.5% of the angle off per
frame, so it dies in about five seconds. The camera's motion numbers taken in
the first minute after a boot were the camera's own exposure settling on a
fresh modeset, not the cube. `glcube` now tops the spin back up to its
resting value whenever no finger is down and the coast has fallen below a
floor. Measured: motion 8.0 / 10.1 / 9.9 at 30 s, 2 min and 4 min.

**The power source decides whether the tablet lives.** The RK816 classifies
the source by its data lines and sets the input current limit from that, and
the battery line on the rescue screen made the difference visible:

| source | detected as | input limit | brightness 40 | brightness 255 |
| --- | --- | --- | --- | --- |
| powered 12 V USB hub | `NONE USB` | 450 mA | +170 mA | -300 to -450 mA |
| USB-C hub | `CDP1.5A` | 1500 mA | +989 mA | +551 mA |

On the 12 V hub -- which sounds like the stronger source and is not, because
its data lines do not advertise a charging port -- the tablet reported
`Charging` with the backlight at the maximum `/init` sets, and ran the
battery to 0% at 3.36 V. On the USB-C hub, whose port enumerates as CDP, it
charges at full brightness. What matters is what the port *advertises*, not
the supply behind it; and `current_now` is the number to read, the status
word is not. (The session first wrote this table the other way round; the
owner corrected which hub was which.)

Images: `boot` = `recovery-taq102-v36-appliance.img` (build `c50d311`);
`recovery` still v35 (`9dc924d`), which differs only by the glcube it does
not run.

## The flicker was the VOP's IOMMU

2026-09-04, 02:40. I saw the panel flicker "like a horror film" on
the appliance and the webcam saw nothing, four recordings running: every
band flat to 0.3 of brightness at 10 to 26 fps. That gap was the clue. A
33 ms exposure averages two panel frames, so what the eye caught and the
camera could not was frame-by-frame alternation. The chain that found it,
each step against my eyes because no instrument here could see it:

| test | flicker | what it rules out |
| --- | --- | --- |
| rescue-screen, one buffer, no flips | no | backlight, panel, LVDS link, power |
| `fliptest`, two identical buffers, 55 flips/s | yes | content, GPU |
| `fliptest` with `FLIP_SAME=1`, same buffer, 55 flips/s | no | the commit / `cfg_done` cycle itself |
| glcube on v37, VOP without IOMMU | **no** | -- |

Along the way, each measured: the battery (charging at +551 mA, still
flickering), DDR frequency scaling (one transition since boot), GPU
frequency and power domain (none, always on), the VOP `bus_error` interrupt
(enabled by the driver, never raised), ghost touches (0 events in 5 s), and
a register sweep of the whole VOP block at 55 flips/s that changes exactly
two words per flip: `WIN0_YRGB_MST` and `REG_CFG_DONE`.

So the flicker came with the *address*, not with the commit: every frame
that starts at a new IOVA pays the IOMMU's page-table walks and the VOP's
first lines arrive late, below the threshold of any error flag.
`make-hybrid-dts.py` now drops `iommus` from the VOP node; rockchip-drm then
allocates contiguous CMA buffers from its 24 MiB pool at 0x88000000 (the
summary shows `buf addr 0x88600000` where it showed the IOVA `0x00258000`),
glcube runs at the same 54.8 FPS, and the picture is steady. The stock
kernel keeps its IOMMU and does not flicker; how its allocation or its
`rk_iommu` differs is not yet known and does not need to be for the
appliance.

The generated tree the tablet runs is now tracked as
`kernel/rk3126-taq102-hybrid.dts`; `boot` holds
`recovery-taq102-v37-noiommu-appliance.img` (kernel v29, ramdisk v36,
resource with this tree). `recovery` is unchanged, v35 on the stock kernel.

One more thing the camera did catch while flipping slowly: a 250 ms blackout
after `[drm] flip_done timed out` followed by two `vop_crtc_enable` -- a
page flip whose completion never came, once in twenty slow flips and never
in 45 minutes of glcube. Separate, rare, and left open.

## The charger limit was thrown away on every boot

2026-09-04, 02:45. With the cable already in at power-on, `rk816-bat` logs
`set charger type: CDP1.5A, input=1500` and four milliseconds later
`NONE DC, input=450`. The DC-jack detection, on a board with no jack, takes
the `DC_TYPE_NONE_CHARGER` path, sees `usb_in`, and writes 450 mA over the
limit the USB detection just chose. That is why the tablet drained at full
brightness on every boot and charged only after a re-plug, and why the
evening's battery numbers depended on when the cable had last moved. The
stock kernel logs the same two lines and has the same defect.

`kernel/patches/0004-*` remembers the current the USB detection chose and
re-applies it on that path. Measured on v38, cable in from power-on,
brightness 255: `NONE DC, input=1500`, `current_now +584 mA`. `boot` =
`recovery-taq102-v38-charger-appliance.img` (kernel v38, ramdisk v36,
resource without the VOP IOMMU).

### Mainline says the same thing, in 2023

Searched 2026-09-04 after the fact. Alex Bee's series "[PATCH v2 00/27] Add
HDMI support for RK3128" (LKML, 16 December 2023) reads:

> The VOP has an IOMMU attached, but it has a serious silicon bug:
> Registers can only be written, but not be read. As it's not possible to
> use it with the IOMMU driver in it's current state I'm not adding it here
> and we have to live with CMA for now. I got response from the vendor,
> that there is no possibility to read the registers and an workaround must
> be implemented in software in order to use it.

And mainline's `arch/arm/boot/dts/rockchip/rk3128.dtsi` ships the
`vop@1010e000` node with **no `iommus` property and no `vop_mmu` node at
all**: the RK3128 VOP runs on CMA upstream, which is exactly the tree this
tablet now boots. The vendor 4.4.167 driver carries the other half of the
story as a quirk, `skip_read` ("rk3126/rk3128 can't read vop iommu
registers", enabled by `rockchip,skip-mmu-read`) which no rk312x device
tree in that tree sets, and its `rk_mk_pte()` leaves the PTE cache and
prefetch bits at the bus defaults with a `TODO` above it. Nobody documents
flicker specifically; what is documented is that this IOMMU is broken
silicon that mainline refuses to drive. Dropping it is not a workaround,
it is the upstream configuration.

Nothing found on the rk816 DC-detect override; the vendor repository's
issues do not mention it.

## The rescue screen got an iOS status bar

2026-09-04, 04:20. Top right of the amber screen: a Wi-Fi fan (a dot and
three arcs, lit from -55 dBm for three, -70 for two, one below), the battery
percentage, and a battery outline with a nub whose fill is the charge --
green while current flows in, red at or under 20% -- and a bolt while
charging. Every shape is a pixel test into the shadow buffer; nothing is
shipped but the binary. `RESCUE_DUMP=<file.ppm>` writes each painted frame,
which is how the bar was checked without a camera
(`docs/evidence/2026-09-04/rescue-screen-ios-frame.png`).

`boot` = `recovery-taq102-v39-appliance.img`, `recovery` =
`recovery-taq102-v39-stockkernel-rescue.img`, both build `70968c1`, BCB
zero. Worth knowing when reading the bar in `recovery`: that image runs the
stock kernel, whose rk816 driver still carries the DC-detect override that
patch 0004 fixes in ours, so it shows the battery draining on a port where
`boot` charges. The bar is telling the truth about that kernel.

## The touch was never alive on our kernel, and the console blank made sure of it

2026-09-04, 16:50. "The tablet is stuck": the cube dead still, no touch, and
I sees the flicker again. Underneath, the system was fine -- up 11
hours, glcube at 54.8 FPS, page flips alternating between its two buffers,
battery full, Wi-Fi at -36 dBm -- and every symptom had a cause that the
device could state over ssh.

**The GSL3673 was held in reset.** `/sys/kernel/debug/gpio` showed the
touch controller's reset line, `gpio2 14` (`gpio-78`), driven low, and the
chip NAKed every i2c read. The driver's probe raises that pin; what lowers
it is `gsl_ts_suspend`, hung off an fb-blank notifier in `tp_suspend.h`.
The kernel command line carries `console=tty0` and `consoleblank=600`, so
ten idle minutes after boot fbcon blanked fb0, the notifier put the touch
controller into reset, and nothing ever unblanks a KMS appliance. Raising
the pin by `devmem` made the chip answer (`0xe0 = 0x80`); writing 0 to
`/sys/class/graphics/fb0/blank` ran the resume path. `taq102-app` now
unbinds fbcon from fb0 before starting anything, so the vt blank timer has
no framebuffer to act on.

**And the firmware was the wrong one.** With the reset released the chip
ran its firmware (`0xb0..0xb3 = 5a`) and still raised no interrupt for a
finger: six interrupts in eleven hours, all from the firmware download at
boot. The vendor tree's `gsl3673.h` carries 4 950 records and a config
whose resolution word reads `0x08000600`; the array lifted from the stock
kernel image (see the brain page) has 4 719 records and `0x02580400`. It is
another panel's firmware. `kernel/patches/0005-*` replaces both arrays with
the stock ones; kernel v40 is v38 plus that patch.

**The cube stopped because of a phantom finger.** The driver's suspend
clears tracking ids for slots 1 and up, never slot 0, so a contact parked
there blocked the resting spin from `c50d311` for good, and the cube sat
still while rendering at full rate. The camera cannot tell a frozen cube
from a spinning one right after a restart -- its auto-exposure produced
`motion 10.5` on a still picture -- so the instrument is now the scanout
buffer itself: `dd` the two page-flip buffers out of `/dev/mem` a second
apart and compare hashes. glcube now treats a contact that has said nothing
for half a second as lifted, and reactivates it on the next position.

**The status bar rides on the cube.** `statusbar.c`, `status.c` and
`canvas.c` are the rescue screen's drawing, split out so both programs
share it; glcube paints the bar into a texture once a second and blends it
over the picture. The first attempt drew nothing: the overlay quad is wound
clockwise and the cube's face culling ate it.

**The power button sleeps and wakes, as Android did.** glcube reads
KEY_POWER from the rk8xx power key. Off: backlight `bl_power` to 4, fb0
blank to 4 (the touch controller into reset through the same notifier that
bit above), and the CRTC taken down, which switches the LVDS panel off.
Then it blocks on the button; touch events are drained. On: the next frame's
modeset brings the panel back, and only then the backlight and the touch.
pwm-backlight on this tree does not follow fb blank (measured: 255 through
a blank), hence the explicit write. Wi-Fi and ssh stay up in sleep.

Two build traps paid for today: `make <pkg>-rebuild` does **not** re-sync a
local package's source into the build directory -- two "fixed" binaries
were the same stale bytes, and `<pkg>-dirclean` is what re-syncs; and the
VM can read a **short copy** of a source file through the shared mount,
which showed as `missing terminating " character` at a line that was fine
on the Mac. Hash the file on both sides before building.

`boot` = `recovery-taq102-v40-appliance.img` (kernel v40 = v38 + patch
0005, ramdisk v40, resource unchanged), flashed and verified 16:47, booted
`20260904-143936-5304230`: fbcon unbound at 5.1 s, reset pin high, firmware
running, cube moving, bar drawn. `recovery` stays v39.
`recovery-taq102-v40-stockkernel-rescue.img` is packed and not flashed.
What only I can check: a finger on the glass (interrupt count on
line 130 of `/proc/interrupts` must climb), the button, and the flicker.

## The wobble was the resting spin, and the tablet learned which way up it is

2026-09-04, 17:25. With the touch alive I reported the picture
"wobbling a millimetre every second". Two instruments that could not see
it: the CPU governor pinned (`interactive` was switching frequency twice a
second, no change) and the touch chip held in reset (no change). The camera
then said what the eyes could not separate: a static bar region jumps with
glcube running and is dead flat with glcube frozen (`SIGSTOP`), and phase
correlation of that region finds **zero pixel shift** in both -- the jumps
were exposure, from the cube's light. Nothing on the panel moves. What
moves once a second is the cube: the resting spin from `c50d311` let the
coast decay to half the resting rate before topping it up, a kick every
0.84 s. The rate is now held at the resting value once the coast reaches
it. The frame-difference `YAVG` is a brightness meter, not a motion meter;
`tools/panel-camera` gained nothing, but the two scripts live in the session
scratch as `wobble.sh` and `shift.py` and are worth reviving if needed.

The status bar sits 12 units from the right edge (the bezel covers a few
millimetres of panel, the nub of the battery was cut off), shows the bolt
and the green whenever the USB port is online (iOS semantics; the rk816
reports 0 mA at 100%), and is painted at four times the size and averaged
down, with a 5x7 face for the percentage.

**Orientation from the accelerometer.** The device tree calls the sensor at
`0x18` an STK8BAxx and the kernel has no driver for it (`sensor_chip_init:
ops is null`); the chip's `WHO_AM_I` at `0x0F` reads `0x11`, which is the
Silan SC7A20, a LIS3DH-compatible part. `accel.c` wakes it over
`/dev/i2c-2` (50 Hz, high resolution, +-2 g) and glcube samples it every
ten frames. Y runs along the short side of the screen: +966 mg with the
picture upside down. Above +500 mg for three samples the picture, the bar
and the touch coordinates turn half a turn; below -500 mg they turn back.

Live on the tablet at 17:22 (glcube replaced in the running ramdisk), then
packed as v41 and flashed to `boot`.

## The shimmer was the PHY's own PLL, and the fix is the vendor's divider pair

2026-09-05. Two days of "a small wave of water passing over things, like
electric noise", on any content, on a fixed picture, with occasional coloured
flashes. What finally measured it was not the eye and not brightness: a
single-pixel vertical line pattern (`src/testpattern.c`, one dumb buffer, one
modeset, no flips, no GPU) recorded by a **motionless** camera, and the
horizontal phase of that pattern extracted row by row. The artifact is a
sideways displacement of the picture, not a change in brightness, which is why
every brightness instrument used before had said "flat".

My own report ranks the patterns and names the mechanism: single-pixel
vertical lines are unwatchable, horizontal lines less so, flat grey shows a
little, **flat white shows nothing at all**. With every data bit at one, a
mis-sampled bit is still one; the artifact scales with how much the LVDS data
toggles, so it is bit errors on the link.

`kernel/patches/0001` had set the PHY's PLL to `prediv 12, fbdiv 175` --
350 MHz, exactly seven times the 50 MHz pixel clock -- reasoning that the
serial clock must be 7x or the data slips. The vendor's own driver
(`drivers/video/rockchip/transmitter/rk31xx_lvds.c`, the legacy FB path Android
ran on this tablet for years) uses `prediv 2, fbdiv 28` = 336 MHz, which is not
7x anything on this board. Measured against the panel, 60 frames each:

| PHY PLL | picture movement, rms | peak to peak |
| --- | --- | --- |
| 350 MHz (`prediv 12`), three variants | 0.63 - 0.81 px | 3.0 - 3.7 px |
| 350 MHz, vendor register order | 0.15 px | 0.63 px |
| **336 MHz (`prediv 2`)** | **0.005 px** | **0.02 px** |

A hundredfold, reproduced twice live and again from a cold boot with the fix
in the image (0.06 px rms). The likely mechanism is the prediv itself rather
than the frequency: dividing 24 MHz by 12 runs the PLL's phase detector at
2 MHz, where dividing by 2 runs it at 12 MHz, and a loop that compares its
phases six times less often is a jittery one. The "must be exactly 7x" reading
was taken during the era when LDO6 was off and the panel unpowered -- one of
the results this project already knew to treat as void.

Ruled out along the way, each by a live experiment that was reverted after it:
the renderer (a static scene page-flips two bit-identical buffers), the VOP's
dither (`DSP_CTRL0` reads `out_mode` P888 with every dither bit clear), the
LVDS data format (18-bit vs 24-bit in `GRF_LVDS_CON0`), the pixel clock
(50 MHz vs 49.5), the LVDS common-mode voltage (`REGE4` VOCM 2 vs the vendor's
0), the CPU governor, and the touch controller held in reset. `src/lvdsdiag.c`
holds all of it: it does a clean modeset, applies one named PHY variant over
`/dev/mem`, and holds a pattern, so any of these can be re-tested in seconds.

Two traps this cost time to see through. **A hand-held camera cannot measure
this**: the rolling shutter turns hand tremor into exactly the same zigzag, and
the first recording measured +-2 px of my pulse. **Frame-difference
brightness is not a motion meter**: it reported "flat" throughout, and the
whole earlier flicker hunt had leaned on it. The measurement that works is the
phase of a periodic pattern, split into a fixed bend (the panel's own geometry,
about 1 px, which nobody notices) and the frame-to-frame movement, which is
what the eye reads as a wave.

The kernel tree also carried an **unrecorded** `REGE4 = 0x80` write that was in
no patch and in no built module; it is gone, and `kernel/patches/0001` now
matches the tree.

`boot` = `recovery-taq102-v43-appliance.img` (kernel v40, ramdisk
`20260905-002916`, PHY module rebuilt with the vendor dividers), flashed and
verified. Evidence in `docs/evidence/2026-09-05/`.

The three tools ship in the image now, as `package/taq102-diag`: they were
built by hand and copied into tmpfs while the shimmer was being chased, which
means they died with every reboot and had to be rebuilt to ask the panel the
same question twice. `recovery` was rewritten at the same time with the
matching stock-kernel rescue, so the way back carries today's fixes as well;
it was written and read back byte for byte, then proved by a BCB round trip:
`boot-recovery` at raw sector 32800, reboot, and the tablet came up on the
stock 4.4.103 with build `20260905-002916`, the amber screen drawn, Wi-Fi and
ssh up and the diagnostics present; zeroing the same sector and rebooting
brought the appliance back with the PHY PLL at 336 MHz
(`docs/evidence/2026-09-05/rescue-v43-round-trip.jpg`). The USB console was
held open throughout as the way back that does not need Wi-Fi -- `stty -f
/dev/cu.usbmodem* 115200 raw -echo`, a backgrounded `cat` on it for the log,
and `printf 'root\r' >` it to log in; `screen -X stuff` never reached the
getty and wasted a few minutes. Worth knowing: the rescue screen does not
rotate with the accelerometer the way glcube does, so on a tablet held the
other way up it reads upside down. Note that `tools/flash-recovery.sh` also sets the BCB and reboots into
what it just wrote, which is the right thing when the point is to test the
rescue and the wrong thing when the point is to keep the appliance running:
this write used `rkdeveloptool wl 196608` directly and left the BCB zero.

## The control centre, and the tablet minds its own brightness

2026-09-07, the second run of the day. I asked for three things: a
brightness that adjusts itself, a screen that switches off after a while
without USB, and a control centre like iOS. And for the look to stop reading
as MS-DOS. The design is in `docs/2026-09-07-control-centre-design.md`
and the plan that built it in `docs/2026-09-07-control-centre.md`;
the reviewer reviewed the first draft (ten blocking issues, all settled in the
second) and implemented most of the tasks, one bounded run each.

**One typeface.** Inter (SIL OFL, `br2-external/package/taq102-fonts/`) is
rasterised on the device by `stb_truetype` into the same canvas the bar and
the rescue screen paint (`src/font.c`), anti-aliased and blended straight
alpha over what is there (`src/canvas_blend.c`). Digits sit in equal cells,
so the percentage does not jitter. The 3x5 bitmap font stays only as the
fallback when the font file is missing.

**The panel.** A swipe down from the bar opens a card over the dimmed cube:
Wi-Fi (name, address, signal, tap to toggle), a tall brightness column that
fills with amber as you drag it, Auto, "Screen off now", "Screen off after"
(1, 5, 15 minutes or never), and Rescue and Loader, which need a second tap
within three seconds. The footer carries the battery, the build and the
kernel. `src/control_center.c` is the model and the hit testing,
`src/control_center_paint.c` the painter, `src/control_center_layout.h` the
one place every rectangle lives. The panel is a second texture over the
scene, updated with `glTexSubImage2D` only when something changed, and only
the rectangles that changed are repainted: a full repaint of the card cost a
frame or two, measured as 52.8 FPS with the panel open.

**Touch, again.** `src/touch_input.c` decodes protocol B and emits contacts
only at `SYN_REPORT`, so a tap that begins and ends inside one frame still
arrives as down then up (the old loop drained events and could lose it);
silence cancels a contact instead of releasing it, so a finger resting on a
button cannot press it by going quiet. `src/touch_router.c` owns capture
from first contact to release: a contact from the bar zone is an opening
candidate the cube never sees, and while the panel is visible every contact
is the panel's.

**Brightness that adjusts itself.** There is no light sensor, so "itself"
means by what the battery does (`src/power_policy.c`, readings every 10 s,
decisions every 30 s from three valid readings): battery alone 128; plugged
and Full, maximum; plugged otherwise, start at 40 the first time a source
is seen, then step down 16 while the battery loses more than 50 mA, step up
16 while it gains more than 100 mA unless that level failed within the last
ten minutes, and hold in between. At the floor with the battery still
draining the footer says "cannot sustain" instead of pretending. A drag on
the slider switches Auto off; the manual level is separate. Plugged means
`usb/online` or `ac/online`, never the sign of the current: a full battery
on a charger reads 0 mA.

**Sleep.** After the chosen minutes with no touch and no supply online the
screen sleeps the way the power key does (backlight off, fb blank so the
touch chip suspends, CRTC off). The power key wakes it, and so does picking
the tablet up: two seconds after sleeping the accelerometer takes a
baseline and three samples further than 150 mg on any axis wake the screen
(`src/sleep_state.c`). Touch cannot wake it: the chip is in reset.

**Settings** live in `/data/taq102.conf` (`brightness_auto`, `brightness`,
`sleep_minutes`), written whole through a temporary file and `rename` on
release or two seconds after the last change. Rescue writes the BCB at raw
sector 32800, reads it back and only then reboots (`src/reboot_target.c`);
Loader execs `reboot-loader`; Wi-Fi runs `taq102-wifi up` or `down` from a
worker thread with a 45 s limit (`src/action_worker.c`), the name read from
the `ssid=` line of `/data/wifi.conf` and nothing else.

The panel offered "Turn on" for a Wi-Fi that was already associated, reported
2026-09-07. `taq102-wifi up` and the app are both `::once` entries in inittab,
so they start together and the link is seconds away: the module load alone
waits up to eight, then association and DHCP. The runtime latched its wanted
state from that first sample, which was always "no link", and nothing ever
revised it -- so the status bar drew bars while the panel said off. It now
adopts an observed link as the wanted state on every read, except while one of
its own actions is in flight, since a toggle to off owns the state until its
worker exits and `down` has removed the module by then
(`src/control_runtime.c`, covered by `runtime_test`).

### What the wrong state cost, read off the tablet the same night

The tablet was plugged in an hour later and the USB console told the rest of
the story. Wi-Fi was down: `wlan0` UP with `NO-CARRIER`, no address, and
`RTW: rtw_set_802_11_connect(wlan0)` repeating in `dmesg` every minute or two
for the best part of two hours. `ps` said why -- **two** `wpa_supplicant`, two
`udhcpc` and two `mdnsd`, all on `wlan0`. The panel had offered "Turn on" for
a Wi-Fi that was already associated, the tap ran `taq102-wifi up` a second
time on a boot whose own `up` had already succeeded, and `load()` returns
early when `wlan0` exists, so the script went straight on to start a second
supplicant. Two of them reset each other's association and neither ever
finishes.

Killing the later set alone did not recover it. One clean `wpa_supplicant`
associated at -32 dBm in fifteen seconds and `udhcpc` took the lease at
192.168.1.51. So `up` is now idempotent: it exits at once when the interface
is already associated and addressed, and otherwise kills whatever is left over
before starting anything. The same edit passes `-O /var/run/wpa_supplicant`,
because `wpa_passphrase` writes no `ctrl_interface=` line and without one the
script's own `status` and `scan` could never reach the daemon -- which is why
`wpa_cli` answered "No such file or directory" all evening.

One more thing the console settled: `/proc/net/wireless` lists the interface
from the moment the driver registers it, associated or not, and an
unassociated one reads `0 0. -256. -256.`. `status_wifi_bars` took -256 dBm as
a level and lit one bar on a radio connected to nothing, so `read_wifi` now
only counts a level that could have come from a radio (`src/status.c`).

### The status bar was not the iPhone's

v48 fixed the state and my next words were that the icons were big
and ugly and looked nothing like iOS. Reading the panel's own scanout settled
it -- `/sys/kernel/debug/dri/0/summary` gives the framebuffer address and
`dd if=/dev/mem` the pixels, which is how
`docs/evidence/2026-09-07-control-centre/statusbar-icons-before-after.png` was
made, and the same read shows the panel open with `TestNet`, the address
and -35 dBm, which is the state fix proved on glass.

The battery was 13 by 7 units beside 18-pixel digits: half again taller than
the type next to it, too stubby at 1.86 wide to tall, and outlined at h/9,
which at that size is a chunky capsule rather than a hairline. The Wi-Fi fan
came to a point because its sector was 45 degrees each side. The iPhone keeps
its battery near the height of its type, about 2.15 times as wide as tall,
with a hairline outline and a fan that is wide and shallow. So the icons are
now 5 by 11 units, radius h/3, outline h/10, the arcs 55 degrees each side and
thinner, and the three gaps tightened to match; the greens and reds are the
real systemGreen and systemRed rather than approximations.

The loop that made this quick is worth keeping: `statusbar_test` already
writes a PPM of the bar, so each attempt was rendered and looked at on the Mac
in a second, and only the last one was built into an image and flashed.

**Overrides** for tests: `GLCUBE_TOUCH` and `GLCUBE_POWER` (input devices),
`GLCUBE_SETTINGS` (the conf path), `GLCUBE_FONTS` (the font directory),
`GLCUBE_TRACE` (the per-second line now carries uploads/s, glGetError and
the panel state). `touchsim` (in `taq102-diag`) creates uinput touch and
power devices and plays taps, drags and swipes; `tools/test-control-centre-device.sh`
drives the whole panel through it from the Mac, reads the scanout after
every step and holds four 60-second phases at 54.8 FPS.
`tools/test-control-centre-host.sh` builds and runs the seventeen host tests
under the sanitizers, fonts included.

`boot` = `recovery-taq102-v47-appliance.img` (kernel v44, ramdisk
`20260907-202158-bb04db0`, v46 plus three review fixes), `recovery` the
matching stock-kernel rescue,
whose backlight now boots at 40 so a plain USB port charges it. The rescue
screen is set in Inter too.


## Linux 7.3 runs on this tablet (2026-09-08)

Battery, storage, Wi-Fi, USB and the panel, on mainline. The kernel console is
visible on the tablet's own screen at the panel's real 1024x600, the RK816
driver reports true battery values, `/data` mounts off the eMMC and `rtw88`
takes a DHCP lease.

Getting there turned up **two bugs in mainline itself**, neither specific to
this board: a genpd deadlock that blocks any driver attaching to a Rockchip
power domain, and an LVDS driver that hides its own panel behind a bridge whose
funcs it then overwrites. Both are written up with before-and-after evidence,
along with the seven hypotheses that were wrong and the two mistakes in method
that cost the most, in `kernel/mainline/FINDINGS.md`.

The one that is worth carrying into any future debugging on this board: **a
diagnostic channel that depends on the thing being diagnosed is not a channel.**
Six attempts produced no output because every channel tried -- the USB gadget,
ramoops, a backlight beacon -- needed something that was not there. What worked
was the framebuffer U-Boot leaves scanning out, `uboot_logo=0x02000000@0x9dc00000`:
already lit before the kernel runs, needing nothing from it. The white screen we
had all been staring at was the channel.

## The cube runs on mainline (2026-09-09)

Fourteen seconds after power-on, v66 is drawing `glcube` through Mesa's lima
driver on the Mali-400 at 52.8 FPS, 1024x600, no errors. The kernel is the one
that first showed the panel (variant M, unchanged); what was missing was a
`gpu-sched.ko` left out of the ramdisk and a `&gpu { status = "okay"; }` the
board DTS never had, because `rk3128.dtsi` ships the GPU disabled.

Two images were spent first on the tempting shortcut: building the whole
display stack into the kernel, once plain and once with the one-line genpd
"fix" this project had queued. Both hang before userspace. The narrow fix is
not that line, and `fw_devlink=off` with the PHY as a module stays the
arrangement. Details and evidence in `kernel/mainline/FINDINGS.md` and
`docs/evidence/2026-09-09-cube/`.

Seventy-three seconds later it froze. lima's devfreq had been driving the
Mali from 148.5 MHz up to 480 MHz over the SoC's OPP table with no regulator
to raise the voltage with it, and at 480 MHz the core stopped answering for
good. v67 removes the OPP table from the GPU node; the clock stays where the
bootloader left it, the cube draws at the panel's rate, and the table comes
back when the RK816 regulators are described on mainline.

The touch controller then turned out to NAK its own reset command while
obeying it, which `silead.c` took for a dead chip; one scoped quirk and the
GSL3673 probes. And the mainline series got its first real review: a PHY
leak, a missing binding, an unscoped quirk, stale messages. It is now twelve
`git am`-able patches that reproduce the running tree line for line
(`kernel/mainline/FINDINGS.md`, "The series, reviewed").

## The Wi-Fi wedge is not a rail, and mainline cannot undo it (2026-09-10)

After any warm reboot on mainline the RTL8723CS enumerates on SDIO and rtw88
fails its power-on sequence at the chip's own power-ready flag (`failed to poll
offset=0x6 mask=0x2 value=0x2`, `mac power on failed`), so there is no wlan0.
The backlog said to cut the chip's rail through the RK816 next. There is no
rail to cut, the chip is not broken, and a full power-off does not clear it.
All three were measured rather than assumed, over the USB console.

**No rail cuts it.** Each of these was followed by an unbind and bind of
`10218000.mmc`, which re-enumerated the card every time and failed the same
poll every time: gpio2 PB5, the pwrseq reset line, held low for a second with
the level read back on `EXT_PORTA`; gpio2 PB1, the vendor `BT,reset_gpio`,
already low; the RK816's 32.768 kHz clkout2 turned off for three seconds with
PB5 held low, which stops the chip's power FSM outright; and RK816 LDO4, LDO5
and LDO6, each cut for half a second and then all three together for three
seconds, enable registers read back each time. Neither SDIO host names a
supply and the vendor `wireless-wlan` node has no power GPIO, so the chip sits
on an unswitched rail and no kernel has ever taken power off it.

**A power-off does not clear it either.** I powered the tablet down
with USB unplugged and booted it cold; the chip came up wedged.

**The chip is fine.** Flashing the v49 vendor appliance and booting it gives
`wlan0` in five seconds, associated to the AP, mdnsd announcing, ssh answering
-- on the same chip that mainline had just failed on. The vendor 4.4 driver
recovers it from any state, every time.

**It is not a kernel regression.** v59, the first mainline image to reach
userspace and the one boot in the evidence where mainline Wi-Fi worked
(2026-09-08), was reflashed and booted: it fails identically now. The same
binary, the same board, a different chip state. So the difference between that
working boot and every failing one is not in the kernel at all -- it is that
the vendor appliance had run before it.

That is the whole mechanism. The vendor driver leaves the chip in a state
mainline's power-on sequence can start from. Mainline leaves it in one that
mainline itself cannot start from, and because nothing on this board can cut
the chip's power, that state survives a reboot and a power-off alike. Only the
vendor driver clears it.

**The recovery, until the driver is fixed:** flash
`recovery-taq102-v49-appliance.img`, boot it once, then flash the mainline
image back. Wi-Fi works on that mainline boot and on no later one.

Three fixes were tried against this and all three failed, each verified on a
chip the appliance had just cleaned, by booting once with Wi-Fi up and then
rebooting:

- **Power the MAC off at shutdown.** `rtw_sdio_shutdown` only calls the chip's
  own shutdown op and `rtw8703b` has none, so nothing powered the MAC down at
  reboot; taking the ifdown path there, as `rtw_pci_shutdown` does with
  `PCI_D3hot`, changed nothing.
- **Force the card-disable sequence and retry.** `rtw_mac_power_on` recovers a
  MAC it finds already on, but `rtw_mac_power_switch` short-circuits when it
  believes the MAC is off, so the disable sequence is never reached from a
  stuck chip. Running `chip->pwr_off_seq` unconditionally and retrying made the
  chip fail the disable sequence too (`failed to poll offset=0x5f8`), then fail
  power-on again. It ignores both sequences.
- **A longer settle after reset.** `post-power-on-delay-ms = <200>`, the value
  `sun50i-a64-pinephone.dtsi` uses for this same part, did not help either. Note
  that property does not widen the reset pulse, as was assumed when it was
  tried: `pwrseq_simple` releases the line first and sleeps afterwards. The
  one- and three-second GPIO holds above are the real test of pulse width, and
  they failed too. All three changes were reverted.

## The fix was an asymmetric transition table (2026-09-10)

That entry-by-entry diff of the vendor's tables against `rtw8703b.c` is what
found it, and it was the reviewer that did it -- the briefing and its answer are in
`docs/evidence/2026-09-10-wifi/`. Two earlier candidates were already excluded
by reading: both drivers write `REG_RSV_CTRL` to zero to unlock the
ISO/CLK/power registers, and the vendor's external-clock configuration is
compiled out (`CONFIG_EXT_CLK = n`). The real difference is not something the
vendor does extra. It is something mainline fails to undo.

`trans_cardemu_to_carddis_8703b` puts the chip's **12H LDO into sleep mode**
(`0x23[4] = 1`) and asks the SDIO interface to suspend.
`trans_carddis_to_cardemu_8703b`, the transition that is supposed to reverse
that, had exactly one entry: clear the hardware power-down bit. So the LDO
stayed asleep and the interface stayed suspended, and the WLAN MAC never
powered up -- while the SDIO function kept enumerating and CMD52 kept working,
which is why the failure looked like a healthy card with a dead MAC. Mainline's
generic SDIO resume handshake does not cover it either: `rtw_mac_pre_system_cfg`
returns early for 8051 wcpu chips, and this is one.

The vendor's own `CARDDIS_TO_CARDEMU` table has four operations this one
lacked, and patch 0018 adds them: clear the WL suspend bit alongside the
power-down bit, withdraw the SDIO suspend request on local register `0x86`,
poll for the interface to leave the suspended state, and return the 12H LDO to
normal mode.

It works, and it works better than expected: v79 was flashed by way of a reboot
into loader mode, which is exactly what wedges the chip, and it brought the
wedged chip up on that first boot with no vendor appliance anywhere in the
loop. Two further warm reboots, Wi-Fi associated on both. The recovery
procedure above is obsolete.

One correction from the same review, worth keeping because it invalidates a
conclusion drawn earlier that night: the `v77` experiment did not show that the
chip ignores the disable sequence. It showed that `ACT_TO_LPS` failed at its
`0x5f8` poll and mainline aborted the whole sequence there, never reaching the
MCU reset or the terminal transition. The vendor continues past that failure.


**Loader mode needs no buttons from any kernel.** `reboot-loader` works only
where the DT declares `syscon-reboot-mode`, which v59 does not, so it reboots
normally there and the tablet comes back to the same image. The magic can be
written directly instead: `devmem 0x100a0038 32 0x5242C301` then `reboot`, the
PMU register at offset 0x38 that U-Boot reads. That took the tablet into loader
mode from an image with no reboot-mode node at all, and is how the vendor
appliance got flashed without touching the tablet.

Two console facts for next time: log in as `root` with no password after a bare
carriage return, and send the lines of a script one at a time; joining them with
`tr '\n' ' '` drops the separators and the shell rejects the lot. And the
tablet's `/dev/mmcblk1` sector numbering does **not** match the LBAs
`flash-recovery.sh` uses -- the recovery partition's own header does not appear
at LBA 196608 from userspace -- so the BCB cannot be written from the running
system, and a `dd` there would land somewhere unknown.


## Sleep that sticks, and a boot screen of our own (2026-09-11)

Three things I asked for, two of which turned out to be one bug and a
setting rather than missing features.

**It already slept, but only on battery.** `sleep_timer_due()` carried a
`!plugged` condition, so an appliance left on the charger never slept at all
and the whole feature looked absent. The power source no longer enters into it:
five idle minutes is five idle minutes. Unplugging still restarts the
countdown rather than shortening it.

**The pick-up detector was waking it seconds after the button.** Sleep here is
not a kernel suspend: the backlight goes off, the CRTC is disabled and the
process waits on the power key and the accelerometer. The baseline for "has
somebody picked this up" was taken on a stopwatch, a flat two seconds after the
screen went dark -- which is while the hand that pressed the power key is still
on the tablet. The baseline was captured mid-movement and the very next sample
read as a pick-up. It now arms on stillness instead: three consecutive samples
agreeing within 60 mg, compared against each other so a slow drift never
accumulates into a false "still".

The trade-off is deliberate. Put the tablet down and it arms in a fraction of a
second. Hold it in your hand and it never arms, so only the power key will wake
it -- which is the right way round, because a hand that can hold it can reach
the button.

Measured on v87, plugged in, with the timer at one minute: it slept by itself,
and four minutes later `bl_power` still read 4 with **no wake event at all**.
The log to read is `/data/log/taq102-app.log`, not the wrapper's: glcube's
stdout is redirected there, and counting wakes in the wrong file gives a
reassuring zero for the wrong reason.

**The white boot screen was the vendor's own logo**, carried along in every
resource image we build because `make-resource.py` preserved it. It now takes a
replacement: `tools/make-logo.py` composes the Vibra wordmark, white on black,
into the 1024x600 8-bit RLE8 BMP U-Boot reads, and both `logo.bmp` and
`logo_kernel.bmp` are swapped. 11 KB against the stock 80 KB, because two
colours compress well.

One detail worth keeping: the palette is a full 256 entries even though the
picture uses two. A two-entry table is legal and smaller, and at least one
reader treats it as a 1-bit image and refuses to decode -- which would have
been discovered on the tablet rather than on the desk.

**The first attempt drew a smudge**, and the hunt for why is worth recording
because most of it ruled things out rather than finding the fault. The file is
well formed: 600 encoded rows, every one exactly 1024 pixels, runs only with no
escapes, the same shape of stream as the vendor's own and now the vendor's own
palette verbatim, so the picture is the only thing about it that differs from
an image this board demonstrably draws. Pillow decodes it back to the right
picture.

What the hunt did establish is where the boot screen comes from. The kernel
command line carries `uboot_logo=0x02000000@0x9dc00000`, and the kernel
registers a `simple-framebuffer` at exactly that address, `x8r8g8b8`,
1024x600x32, which keeps displaying U-Boot's decode until the Rockchip DRM
driver takes fb0 at about 15 seconds. So whatever is on the panel for those
fifteen seconds is U-Boot's own rendering of `logo.bmp`.

That memory cannot be read back from userspace to see it: `/dev/mem` answers
`Bad address` under `CONFIG_STRICT_DEVMEM`, the region is carved out of
memblock so it never appears in `/proc/iomem`, and by the time there is a shell
`fb0` is `rockchipdrmfb` rather than the simplefb. The next step is a camera on
the boot, not another guess.

Also worth knowing, because it contradicts what everyone assumed: the stock
`logo.bmp` is **DENVER in white on black**, not a white screen. So the white
boot screen this was meant to replace was never the vendor's logo.

## The link learned to wait, and the show qualified itself (2026-09-12)

The desk that first drew the June show on the 12th ran on a link that could
freeze the loop and a map written by hand. Both went the same day, after a
design round with the reviewer against the professional desks (Luminair, Photon,
MagicQ, grandMA3) and a second round against the real show, which is
`Vibra.qxw` on DMX-Fixtures' `qlctool` branch, not the June file.

**The link.** `ws_client` connected with a blocking `connect`, `send_all`
could sit a second per attempt, the first `/vc.json` fetch ran before the
display opened, and a reconnect fetched synchronously; a failed fetch still
went READY on the old routes; and the pong buffer was two bytes short of a
125-byte ping. All replaced: a bounded `send_queue`, a non-blocking connect
and handshake, an incremental `http_fetch`, and `qlc_session`, a state
machine that reaches READY only on a fresh parsed snapshot and names the
reason for every drop. Measured on the tablet against QLC+ 5.2.2 on this
Mac: the display comes up before the network; a master frozen for three
seconds was dropped at 805 ms and relinked by itself; a dead host reports a
connect timeout every ten seconds with touch alive; thirty minutes of
heartbeats gave p50 19 ms, p99 50 ms, worst 319 ms and no drops, so the 750
ms threshold stays (`docs/evidence/2026-09-12-desk-phase1/`).

**The show.** the reviewer read the pinned 5.2.2 sources against the generated
console and found that every solo frame carried `ExcludeMonitored=True`,
which makes `VCButton::notifyFunctionStarting` skip a button that is only
monitoring its function. The colour wheel under COLOR is exactly that when
AUTO started it, so a pick never stopped it and the rig had two colour
sources: the bug the play page was built to prevent, invisible to a checker
with no rule for it. The generator now hears monitored buttons on the seven
handoff frames (the room state, the haze rhythms and the five families) and
keeps the exclusion on the library frames, whose looks are chaser steps;
`qlctool check` gained `marco solo sordo`, which reasons about the graph
rather than captions; the three shows were regenerated and validated. Seen
live through the probe: pressing the Rig Rojo pick while AUTO runs now
stops the wheel, CHARLA stops AUTO and releases the pick, PARAR TODO stops
the rest.

**The map.** `qlctool deskmap` reads the saved workspace and writes the
tablet's map: seven pages by the console's own frames, 132 controls with
widget, function, action, solo frame, two-line captions, roles and the
colours their scenes write, the two dials with the 5.2.2 multiplier table
(0 is None and 1 is Zero, which the old manifest tool read as powers of
two), the StopAll button with its fade and the GrandMaster slider. The desk
validates each control against the live console before enabling it and
disables everything on a QLC+ line the map was not made for. Held hits are
carried disabled: the desk holds nothing open across a network, and the reviewer
showed that a single-shot chaser is no substitute for Flash priority. This
phase lays out the room's seven states and the panic button; the pages come
next.

## The desk got its pages, and learned to paint only what changed (2026-09-12)

Seven pages on the rail, laid out by a resolver from the generated map:
sections with headings, tiles sized by role (a wide AUTO, colour picks as
swatch rings with the colours their scenes write, four haze rhythms to a
row), banks with pills when a page overflows, and the room's seven states
riding every other page in a compact row so AUTO is one tap away from
anywhere. Captions wrap on two lines measured against Inter, never guessed.
A padlock on the rail locks the surface with a tap and unlocks with a held
second; the power key blanks the panel and wakes it locked; the desk never
sleeps by itself, because a desk waiting for a cue is idle only by the
appliance's definition.

The perf gate then failed by a mile: a full repaint cost 217 ms on this CPU,
against a 33 ms budget. Four changes, each measured on the tablet with the
fader dragging itself (`DMXDESK_DRAG=1`, since the mainline kernel has no
uinput to fake a finger): rounded rectangles drawn as row spans instead of a
float test per pixel (217 to 41 ms), the status bar painted once per change
and copied, damage tracked per control with every primitive clipped to it
and the presenter copying only the union of this frame's and the other
buffer's pending damage (41 to 31), and finally the background clear clipped
too: 14 ms median, 16 at p95, present 4 ms, 8 MB resident. A page change
still repaints everything once, in about 41 ms, which is one frame and not on
the drag path. Evidence: `docs/evidence/2026-09-12-desk-phase2/`.

## The gear, and a venue's Wi-Fi without ssh (2026-09-12)

My first ask for the desk was the plainest: at an event, join
whatever Wi-Fi there is, find the Mac running QLC+ or type its address, and
see the battery and set the brightness, all from the tablet. That is now a
gear at the left of the status bar. It opens a settings surface over the rail
and the content while the master column stays live, with two cards and a
footer: the Wi-Fi the tablet is on and what a scan found, the master it talks
to and what a sweep found, a brightness fader and a `Dim on battery` toggle.

The Wi-Fi card speaks to wpa_supplicant directly, over the control socket
`wpa_cli` uses, with a second socket attached for the daemon's events: a
scan ends when the daemon says so, not after a guessed four seconds, and no
SSID or key ever reaches a shell. Joining is a transaction with a way back.
The block goes into `/data/wifi.conf` atomically with the next priority, the
daemon re-reads and is told to select that network by the id it reports,
association is awaited on the event socket, a fixed command renews the
lease, and an address is awaited. A wrong key (the daemon names it when it
disables the network), a refusal, or twenty seconds of silence at any stage
removes the block, re-reads, and selects the previous network again, so a
typo at a venue costs a retry and not the link. A network the tablet already
knows joins on the block it has and keeps its key whatever happens. The key
itself is typed on an on-screen keyboard with three layers covering every
printable ASCII character, masked unless `show` is held, and the transaction
is the only thing that sees it. All of this is proven in the host suite
against a fake supplicant that keeps a network list and pushes the events a
real one would, on every path.

The master card has no discovery to lean on, because QLC+ announces nothing,
so it sweeps: the desk runs itself as a child with `--find 192.168.1.71/24
9998`, the prefix read off wlan0's netmask, sixty-four non-blocking connects
at a time with a 400 ms deadline, and a host is a master when `GET /` returns
`QLC+` in its first two kilobytes. On the tablet a /24 takes two seconds and
lists this Mac twice, Wi-Fi and Ethernet. A tap on a found row, or an address
typed on the keypad, is written to `/data/desk.conf` and the session re-dials
at once; from then on the desk starts without `--host`, and without the file
it starts anyway and says `No master set` on the card. Brightness is the
desk's own: the same settings file glcube keeps, a deferred save so a drag is
one write, and the battery policy of the control centre behind the toggle.
What remains is a finger: the join, the fader and the toggle are my
checks, listed in `TODO.md`; the scan, the sweep and the file-driven link
were run on the tablet and are in `docs/evidence/2026-09-12-desk-phase3/`.

## Speed is two dials, not a show BPM (2026-09-12)

The show has two speed dials and the desk gives each a card on a SPEED page:
`Tempo Show` over the colour and effect chasers, `Vel. Movimiento` over the
movements. A card reads its base time off the master (the snapshot carries
`currentTime` and `currentFactor`, so nothing is a guess even before the
first push) and shows it as BPM large with the milliseconds and the time
multiplier beneath, then Tap, a BPM either way, half and double time, and an
explicit `x1`. That last one exists because the engine's multiplier enum has
two values a desk must never send but a Mac can leave behind: `None` and
`Zero` both multiply by zero in `applyFunctionsTime`, so while a dial sits on
either the steps are dead and `x1` is the way back. A `Tap both` strip under
the cards retimes both from one tap, sending up to two frames, the one
amendment to "one gesture, one frame" that the reviewer allowed, and it is dead
while either dial waits for an echo.

Two engine facts shaped the model, both read in `vcspeeddial.cpp` rather
than assumed. The setters return on equality and push nothing, so a same
value is never sent: a steady tap at the current tempo would otherwise wait
for an echo that never comes and call the master silent. And `SPEED_STATE`
names no sender, so a push the desk did not ask for is `State updated`, never
"changed on master": a late echo of our own would be mislabelled. One change
is outstanding per dial; an echo that matches clears it in silence, and 1500
ms without one makes the dial unconfirmed and asks the session to re-read the
console, which is the recovery for a state the socket cannot otherwise give.
Tap commits on the down edge (tempo is the down edge) as the median of the
last four intervals, with a 200 ms bounce floor and a two-second reset.

On the tablet the cards read 120 BPM from the snapshot, a probe's change on
the Mac's side appeared as 150 with the note, and a run of taps on `Tap both`
retimed both dials with the echo 39 to 179 ms after the touch-down, 56 in the
middle, over one Wi-Fi hop. Evidence in `docs/evidence/2026-09-12-desk-phase4/`.
the reviewer's six answers on the doubts are in `docs/evidence/2026-09-12-desk-speed-findings.md`.

## The interface, reviewed as an operator would (2026-09-12, night)

I said the interface had many defects, and it did. Every page and
bank was dumped from the tablet and looked at before anything was changed,
which found six by eye: the master fader read `--` until somebody moved it
on the Mac, because `desk_add` starts every control unknown and the
validator never handed it the slider's snapshot value; the bottom row of
picks touched the panel's edge, because the content end was written as the
panel's height; bright swatches wore a stray keyline, because the RGB
integer was compared whole rather than by its brightest channel; the link
banner flipped wording every second and a half while the master was down;
the gear was a 56 px lump; and the worst one, found in `dmesg` rather than
in a frame: I had tapped Join at 17:01 UTC, and the join had killed
the supplicant's control socket. `RECONFIGURE` re-reads the configuration
file, and when the file names no `ctrl_interface` the daemon drops the one
it was started with under `-O`, so every request after that first join was
answered by nobody. The writer now puts the line first, and the tablet was
put right with the line and a `SIGHUP`.

Then two second opinions. A reviewer agent read the touch routing and found
four capture defects a frame cannot show: a second finger on the settings
sheet ending the first one's drag, the power key leaving the desk's slot
bookkeeping stale, dead space on the speed page claiming the slot, and a
Scan button painted dead but alive to touch. the reviewer looked at sixteen frames
and the code and returned twenty-eight findings, then ten more against the
diff of the first round. Most were taken. The panic button now takes a
second finger while a cue is held and ends that cue's gesture when it
fires. The lock and the gear cancel every capture, the rail's included.
Tempo is measured from the contact's own timestamp while the echo deadline
runs on the desk's clock, so a late-drained batch keeps its intervals
without calling the master silent. The room's states and the family's own
automation ride every bank in compact form. Colours are clipped segments of
a round swatch, and a pick with no colour gets its name large instead of a
grey ring. A choice running on another bank is named in its heading and its
pill gets an amber dot, amber keeping its one meaning. The settings surface
lost its amber for ink, gained a Close, a New key path for a known network
whose old block comes back whole if the new key fails, checked address
parsing that keeps the keyboard open and says why, and notes where a sweep
or a save fails. Stale supplicant replies are drained before a request so a
late `SCAN` answer never reads as `STATUS`. Another show on the Mac is said
in a banner over the dead tiles instead of `Linked` beside them.

What was not taken is in `TODO.md` in the reviewer's order of worth, the first
being the supplicant requests made asynchronous, since a scan still holds
the loop, panic button included, for up to 300 ms. The findings are in
`docs/evidence/2026-09-12-desk-ui-review-findings.md` and `...-round2-findings.md`,
the frames before and after in `docs/evidence/2026-09-12-desk-review/`.

## The leftovers, and the reviewer writing the hardest one (2026-09-13)

Six things remained from the review, and I said do them all, with
the reviewer. The hardest was the supplicant: every request to wpa_supplicant
blocked the desk's one loop until its reply or its timeout, a scan for up
to a third of a second and a join's `RECONFIGURE` for up to three, with the
panic button hostage meanwhile. That went to the reviewer in a worktree of its
own with a briefing that named the shape (no threads, the control socket
already non-blocking, one outstanding request, deadlines, the join as
stages driven from the loop, rollback as stages too) and the proof (the
same fake supplicant, a silent daemon failing in about three seconds with
the file restored and no step blocking past the poll). It came back in
three commits with thirty tests green and the cross build clean, its own
account of what still waits (the `ATTACH` at startup and the `DETACH` at
exit, outside the loop). A reviewer agent then read the merge as an
adversary, path by path: no request that can hang, no late reply that can
answer the wrong command, no double close, no key in a log line, the
starvation between card and join bounded both ways, and the tests found to
exercise what the account claimed. Two minor points, both about a queued
scan's busy word and clock starting when it was asked rather than when it
went out, fixed here.

The other five were the desk's. A cue that sent its frame now carries a
small ink mark and refuses a second tap until the master answers or a
second and a half passes, so a nervous double tap is one toggle, and the
mark is ink because amber is the master's word alone. Disabled and unknown
tiles share one look: a disabled one sinks to glass and says in plain words
why (`Mac only - hold it there`), an unknown one keeps its tile and says
`unknown`, and a wrapped name never loses that line. The hits held on the
Mac, which filled a bank of dead tiles on LIVE and two on COLOR, ride
compact now, seven to a row, under a heading that says Mac only once. The
safety captions live where the words come from, in the generator: `TODO
NEGRO` reads `look a negro, no un stop` and each haze rhythm `dispara ya`,
because a tile that says `apaga las luces` at a venue gets pressed as a
stop. And the sweep's own button reads Stop while it runs, through a cancel
the action worker did not have.

On the tablet the surface's scan lists the room's networks with the link
untouched; the heartbeat's worst round trip is 40 ms plain and 219 ms with
the sheet open and a scan running, which is a full-sheet paint, not a wait.
Backlog: the lifecycle waits and a dead channel after a failed redial.

## The desk's second design, drawn first and built with the reviewer (2026-09-13)

I looked at the desk and said what it was: a student's project.
Icons made of rectangles, buttons that did not line up, space taken for no
reason, and the things a show is run with, the flashes and the smoke, on a
second page and dead. He was right, and the answer was not another round of
fixes but a second design, drawn before it was coded.

Four professional surfaces were looked at for what they get right (ChamSys
QuickQ and MagicQ for their stable playback positions, Photon for touch
targets that read as targets, grandMA3 for state inside consistent
boundaries), three mockups were drawn at 1024x600 with the bundled Inter
(`tools/mockup.py`) and sent to the reviewer with those references. Its critique
changed the plan in ways worth keeping: a master fader with a track and a
thumb rather than an amber slab, since amber means running and a level is
not; no subtitles on state tiles unless they fit; whole-tile colour targets;
CONTROL kept as a tab; and, on safety, that a cap on the tablet is a local
release deadline and not a bound on the output, because QLC+ has no lease
and a Flash left on by a lost link stays on until the Mac releases it. The
line was drawn there: the light flashes fire from the tablet while held,
with a three-second cap and every release owed until it goes out; the
strobes and the fog stay on the Mac until a finite burst is proven there.

Then it was built in three stages, each dumped from the tablet and looked
at. One bar of tabs and status replaced the rail, with icons rasterised from
vectors at four times the size and blended as alpha (`tools/icons.py`), so
the gear is a gear. SHOW became a fixed composition rather than a flow: the
room's seven states, the hits as holds, COLOR BEAM as the one toggle under
its own word, the fog holds greyed and honest, the ambient rhythms as a
selector with OFF, the rig's ten colours as small tiles, a tempo card with
TAP; nothing on it moves when a section grows, and nothing on it needs a
page. The hold-to-fire model was the reviewer's, written in a worktree against a
briefing that named every rule (one release per press, caps, cooldowns,
link loss, owed releases) and proven by seventy-nine assertions before it
was wired here, per finger and outside the model's single capture; a
reviewer agent then found the one path that could leave a light on, a
snapshot rebuilding the model under a held finger, and it releases first
now. The settings sheet moved onto the content's grid with a header and a
way out where a hand expects it, lost its white slabs, and speaks Spanish,
as does the keyboard and everything the operator reads.

the reviewer looked at the built frames as well and returned another list, most
of it taken the same night: the ambient's own words, the master's readout
above its travel, paired looks saying what the rest of the rig does, the
strobes back on the Mac, a release that could not be sent kept as owed and
painted "apagado sin confirmar". What remains is in `TODO.md`, and what
needs a finger is there too: the desk on the tablet shows SHOW first, and
the flashes wait for my hand.
