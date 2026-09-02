# TAQ-102

Turning a Denver TAQ-102 tablet — Rockchip RK3126C, 1 GB RAM, LVDS 1024×600 —
into a Linux appliance that boots straight into one application.

Hardware notes, the recovered stock firmware and the gate-by-gate plan live
outside this repo: `~/p/brain/personal/denver-taq102-tablet.md`, and the archive
with every image and checksum is `/Volumes/Datos4TB2/denver-taq102/`.

This repo holds the Buildroot `br2-external` tree for the userspace. It builds
**no kernel**: the first milestone runs under the stock vendor kernel 4.4.103,
which already drives the panel, the touch controller, the PMIC and KMS on this
board.

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

## Status

Gate 2 is done: the image builds, our `/init` survives packaging, the binaries
are ARM EABI5, and every image carries a build id in both `/init` and
`/etc/taq102-build-id` so a booted system can be identified with certainty.

Gate 3 — writing it to the `recovery` partition — has not been attempted. It
depends on something still unknown: whether the stock U-Boot accepts a modified,
unsigned recovery image.
