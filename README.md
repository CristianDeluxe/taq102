# TAQ-102

Turning a Denver TAQ-102 tablet — Rockchip RK3126C, 1 GB RAM, LVDS 1024×600 —
into a Linux appliance that boots straight into one application.

Hardware notes, the recovered stock firmware and the gate-by-gate plan live
outside this repo: `~/p/brain/personal/denver-taq102-tablet.md`, and the archive
with every image and checksum is `/Volumes/Datos4TB2/denver-taq102/`.

This repo holds the Buildroot `br2-external` tree for the userspace. It builds
**no kernel**: the first milestone runs under the stock vendor kernel 4.4.103,
which already drives the panel, the touch controller, the PMIC, KMS and the
Mali-400 on this board.

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

## Status

The tablet boots this image, drives the panel through DRM, reads multitouch, and
now renders with the GPU. `particles` is the CPU application, `glcube` the GPU
one.
