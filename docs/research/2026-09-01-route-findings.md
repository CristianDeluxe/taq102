# Denver TAQ-102 (RK3126C) Linux-appliance research

Research date: 2026-09-01

## Evidence convention and scope

- **Verified:** directly supported by the cited source, preferably current upstream source code or official documentation.
- **Corroborated:** supported by multiple independent sources or by source code plus a documented real-world port.
- **Inference:** engineering judgment derived from verified facts; it has not been demonstrated on this exact Denver board.
- **Not established:** the available sources do not justify a conclusion.
- Hardware observations and backup facts in `BRIEFING.md` are accepted as measurements of the actual device and are not re-derived here.
- Old forum posts and abandoned trees are treated as leads, not proof of current upstream support. Source age is called out where it matters.

## Research status

All eight requested sections have been researched and synthesized. The file was written incrementally so the completed evidence survived throughout the run.

## 1. Recommended strategy — complete

### Conclusion

**Keep vendor 4.4 for the first screen-and-touch milestone, but do not replace U-Boot at the same time.** The shortest defensible route is:

1. Preserve BootROM-facing firmware: the stock ID block/miniloader, `trust`, and initially the stock U-Boot.
2. Reuse the stock kernel plus its exact DTB first, and replace only the Android userspace/ramdisk with a minimal Buildroot initramfs. Prefer the recovery slot for the first persistent experiment if the stock U-Boot can select it.
3. Once that boots reproducibly, rebuild the closest Rockchip 4.4 tree with the recovered DT and built-in GSL3673 data, changing one layer at a time.
4. Treat mainline as a parallel engineering track, not as the critical path to the appliance. Mainline is attractive for maintenance, Wi-Fi, Bluetooth, and Lima, but the tablet's RK3126 LVDS output is not wired up upstream.

**Verified:** upstream now has substantial RK3128 support, including an RK3126-compatible VOP and most SoC peripherals, so mainline is no longer accurately described as “RK3128 without display support.” It is more precise to say “core SoC plus VOP and HDMI/DSI, but no upstream RK3126 LVDS bridge/encoder or tablet board DTS.” The current [`rk3128.dtsi`](https://github.com/torvalds/linux/blob/master/arch/arm/boot/dts/rockchip/rk3128.dtsi) and Rockchip LVDS driver's [match table](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/rockchip/rockchip_lvds.c) establish that distinction.

**Inference:** changing the early bootloader, kernel, DT, and userspace together creates four independent failure domains and is unnecessary for the stated goal. The recovered stock kernel/DT already proves panel, touch, PMIC, eMMC, and KMS integration on this exact board. Holding those constant first gives the highest chance of obtaining a debuggable non-Android system quickly.

**Not established:** whether this particular stock U-Boot accepts an unsigned modified `boot` or `recovery` image, whether its `recovery` key path works, and whether its kernel comes from the `kernel` partition while the `ANDROID!` image supplies only the ramdisk. Inspect its environment/strings and unpack both Android images before writing anything.

### Why an existing port does not remove the hard work

- **Verified:** postmarketOS has a remarkably close device page, the [Klipad KL3669](https://wiki.postmarketos.org/wiki/Klipad_KL3669_%28klipad-rel%29): RK3126C, 1 GB RAM, 1024x600, Android 8.1, downstream 4.4.167. It reached USB networking, but its screen is marked broken and touch only partial; flashing is also marked broken. This is valuable evidence and a source lead, not a working tablet-display base.
- **Verified:** the current postmarketOS pmaports testing tree still contains both [`device-klipad-rel`](https://gitlab.com/postmarketOS/pmaports/-/tree/master/device/testing/device-klipad-rel) and [`linux-klipad-rel`](https://gitlab.com/postmarketOS/pmaports/-/tree/master/device/testing/linux-klipad-rel), so this is a concrete source package to inspect rather than only an archived wiki lead.
- **Verified:** the [Proscan PLT9650G](https://wiki.postmarketos.org/wiki/Proscan_PLT9650G_%28proscan-plt9650g%29) port is an older RK3126 effort whose 4.4.189 kernel compiled but did not boot. It is not a reusable success case.
- **Verified:** the [Tigerbox TOUCH](https://wiki.postmarketos.org/wiki/Tigermedia_Tigerbox_TOUCH_%28tigermedia-tigerbox%29) is RK3128, but its documented postmarketOS approach is a chroot under the signed stock system rather than a normal kernel/bootloader port.
- **Verified:** upstream Linux currently has only two RK3128 board DTS files, `rk3128-evb.dts` and `rk3128-xpi-3128.dts`, in the [ARM Rockchip DTS directory](https://github.com/torvalds/linux/tree/master/arch/arm/boot/dts/rockchip). Neither is this tablet class.
- **Verified:** a 2026 [Armbian community thread](https://forum.armbian.com/topic/59146-armbian-for-rk3128-tvbox-board/) is building an RK3128 TV-box port and points to a scripts repository, but it is not an official supported tablet board and does not solve RK3126 LVDS.
- **Verified:** [RK3128-CFW](https://github.com/RK3128-CFW) is a downstream Batocera/Buildroot ecosystem for Powkiddy A12/A13 handhelds. Its hardware and vendor-kernel work can be mined, but it is not an upstream board port for this tablet.
- **Not established:** repository searches on 2026-09-01 found no maintained OpenWrt target or Buildroot board defconfig for an RK3126C/RK3128 tablet in the current [OpenWrt tree](https://github.com/openwrt/openwrt) or [Buildroot configurations](https://gitlab.com/buildroot.org/buildroot/-/tree/2025.11/configs). This is a negative search result, not proof that no private or abandoned port exists; a search hit or downstream fork should not be represented as an upstream-supported port.

## 2. Mainline Linux status — complete

### What is supported upstream today

- **Verified:** the current [`rk3128.dtsi`](https://github.com/torvalds/linux/blob/master/arch/arm/boot/dts/rockchip/rk3128.dtsi) describes Cortex-A7 CPUs, clock/reset and power domains, interrupt controller, VPU, VOP, DSI, HDMI, USB, I2C, SPI, PWM, SD/MMC/SDIO/eMMC, audio, thermal, and other core blocks. The board-compatible layer still has to supply regulators, pin choices, panel graph, peripherals, and enablement.
- **Verified:** the display controller node is compatible with `rockchip,rk3126-vop`. The binding explicitly accepts that string in [`rockchip-vop.yaml`](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/display/rockchip/rockchip-vop.yaml), and the driver has RK3126 register/window data in [`rockchip_vop_reg.c`](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/rockchip/rockchip_vop_reg.c). Thus the VOP itself is supported.
- **Verified:** the current SoC DTS connects the VOP output graph only to HDMI and MIPI-DSI endpoints; it contains no LVDS node or endpoint. This is directly visible in [`rk3128.dtsi`](https://github.com/torvalds/linux/blob/master/arch/arm/boot/dts/rockchip/rk3128.dtsi).
- **Verified:** the upstream Rockchip LVDS driver matches only `rockchip,rk3288-lvds` and `rockchip,px30-lvds`; it does not match `rockchip,rk3126-lvds`. See [`rockchip_lvds.c`](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/rockchip/rockchip_lvds.c).
- **Verified:** MIPI-DSI support for RK3128 was still being added in 2024, illustrating that this SoC's display enablement has been incremental. The relevant [linux-rockchip patch](https://lists.infradead.org/pipermail/linux-rockchip/2024-May/046975.html) is now reflected in the upstream DTS.

### Implication for this device

**Corroborated:** a mainline kernel can plausibly be brought up first on UART with eMMC, USB, and SDIO, and it has a real DRM VOP driver. It cannot drive this stock LVDS path merely by translating the recovered DTB to modern bindings: a compatible RK3126 LVDS driver/bridge and VOP-to-LVDS graph still have to be implemented or ported from the BSP and validated electrically. The absence in both the SoC DTS and the driver's match table is stronger evidence than coarse device-status tables.

**Caution:** the postmarketOS [Rockchip category table](https://wiki.postmarketos.org/wiki/Category%3ARockchip) labels RK3126/RK3128 broadly as mainline/display-capable. Device-specific pages and current upstream source show that this is too coarse to prove this tablet's LVDS path.

**Inference:** start a future mainline board DTS from upstream `rk3128.dtsi`, because the measured stock DT already uses RK3128 compatibles for most blocks. Add a TAQ-102 board file and modern bindings rather than attempting to boot the vendor DTB unchanged. Vendor-only properties and the missing LVDS compatible mean the recovered DTB is a specification, not a drop-in mainline DTB.

## 3. U-Boot and the actual Rockchip boot chain — complete

### Supported pieces

- **Verified:** current mainline U-Boot has `CONFIG_ROCKCHIP_RK3128` in its [Rockchip Kconfig](https://github.com/u-boot/u-boot/blob/main/arch/arm/mach-rockchip/Kconfig), and an [`evb-rk3128_defconfig`](https://qemu.googlesource.com/u-boot/+/refs/tags/v2022.10-rc3/configs/evb-rk3128_defconfig) exists. That defconfig uses `CONFIG_SKIP_LOWLEVEL_INIT` and a text base of `0x60000000`; it does not enable SPL. The current Kconfig likewise does not select `SUPPORT_SPL` for RK3128.
- **Inference:** upstream supports U-Boot proper for RK3128-class hardware, but does not provide a demonstrated all-upstream DDR-init/SPL chain for this RK3126C tablet. A custom board DT/config is still required for its UART, MMC, PMIC, and pins.

### Minimal chain to preserve initially

The safest working model is:

```text
BootROM
  -> stock ID block / DDR init / Rockchip miniloader
  -> stock trust/TOS payload
  -> U-Boot proper in a Rockchip uboot.img container
  -> kernel + DTB + initramfs/root filesystem
```

**Verified:** Rockchip documents two families of chain: its proprietary miniloader flow and U-Boot TPL/SPL. In the miniloader flow, the ID block supplies DRAM initialization and the next-stage loader; `uboot.img` and `trust.img` are separate. See Rockchip's [Boot option](https://opensource.rock-chips.com/wiki_Boot_option) documentation. Its [partition documentation](https://opensource.rock-chips.com/wiki_Partitions) places loader1, loader2, trust, and boot at fixed low-level locations independent of a GPT.

**Verified:** a raw `u-boot.bin` is not the same object as this tablet's `LOADER ` partition. Rockchip's official flow packages U-Boot proper with `loaderimage --pack --uboot`; the tool source defines the [`LOADER ` header](https://github.com/rockchip-linux/u-boot/blob/next-dev/tools/rockchip/loaderimage.c). Mainline U-Boot's [Rockchip board documentation](https://docs.u-boot-project.org/en/stable/board/rockchip/rockchip.html) also describes the `loaderimage` packaging step for 32-bit targets.

**Inference:** replacing only the `uboot` payload may eventually work if it is packaged at the stock load address, has a correct tablet DT, and honors the stock miniloader/trust contract. It is not a safe Phase A prerequisite, and no source found demonstrates that exact combination on the Denver board.

### A dangerous offset ambiguity

**Verified:** Rockchip's legacy *parameter address* and physical eMMC LBA are not always the same. The official partition introduction maps U-Boot parameter address `0x2000` to physical LBA `0x4000`, and trust parameter address `0x4000` to physical LBA `0x6000`; loader data begins at physical LBA `0x40`. See [Rockchip Introduction to Partition](https://nnewn2021.site1.hwcloudsite.cn/upload/Rockchip_Introduction_Partition_EN_uc0k.pdf) and the [partition wiki](https://opensource.rock-chips.com/wiki_Partitions).

**Conflict requiring measurement:** the briefing's verified contiguous ranges call the first 8,192 sectors `idbloader` and the next range beginning at 8,192 `uboot`. That may be a logical/parameter view rather than the physical LBAs expected by a raw `rkdeveloptool wl`, or it may reflect this vendor's actual layout. Do not resolve this from naming. Before any write, probe the intended physical LBA with `rkdeveloptool rl`, locate the observed `LOADER ` / `TOS ` / `RSCE` / `ANDROID!` magic, and compare the complete sector range byte-for-byte to the corresponding saved image. A command using the wrong coordinate system could overwrite the early loader.

### `boot0`, `boot1`, `trust`, `misc`, and Rockusb

- **Verified:** the normal documented Rockchip flow uses fixed offsets in the eMMC user area; the mere existence of standard eMMC `boot0`/`boot1` hardware partitions does not prove the SoC boots from either one. A 2024 U-Boot [proposal to add optional boot-partition FIT loading](https://lists.denx.de/pipermail/u-boot/2024-February/545124.html) further distinguishes that optional path from the ordinary one.
- **Not established:** the TAQ-102's eMMC `EXT_CSD PARTITION_CONFIG`/`BOOT_PARTITION_ENABLE` value has not been reported. Read it before forming any conclusion, and do not write `boot0`, `boot1`, RPMB, or EXT_CSD boot configuration in this project.
- **Verified:** `trust.img` is a separate trusted-OS payload in the miniloader flow; Rockchip's [boot documentation](https://opensource.rock-chips.com/wiki_Boot_option) identifies the ARMv7 payload as `tee.bin`. Keep stock `trust` unless there is a specific audited reason to change it.
- **Verified:** Android's `misc` partition contains the bootloader control/recovery message. See AOSP's [`bootloader_message.h`](https://android.googlesource.com/platform/bootable/recovery/+/main/bootloader_message/include/bootloader_message/bootloader_message.h) and Rockchip U-Boot's [boot-mode handling](https://github.com/rockchip-linux/u-boot/blob/next-dev/arch/arm/mach-rockchip/spl_boot_mode.c). Preserve it; an incorrect bootloader-control message can cause recovery/boot-mode surprises.
- **Verified:** in Rockusb, `rkdeveloptool db` downloads and executes a temporary USB plug/miniloader in RAM, whereas `ul` upgrades the ID block on storage. Rockchip documents the distinction in [Rockusb](https://opensource.rock-chips.com/wiki_Rockusb). Phase A should use `db` when needed and must not use `ul`.
- **Inference:** identify accessible mask-ROM pads or the eMMC clock test point before experimenting with early-loader storage. Loader mode is not a recovery guarantee after corrupting the ID block; mask-ROM access may then require opening and shorting hardware.

## 4. RTL8723CS Wi-Fi and Bluetooth — complete

### Wi-Fi

**Verified:** the best-maintained 2026 path is upstream `rtw88`, not the old staging `rtl8723bs` driver and not an unmerged `8723cs` vendor fork. Current Linux contains [`rtw8723cs.c`](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/realtek/rtw88/rtw8723cs.c); [`CONFIG_RTW88_8723CS`](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/realtek/rtw88/Kconfig) selects the SDIO bus support and RTL8703B common chip code. The option remains described as experimental.

**Verified limitation:** the upstreaming report documented ordinary station and monitor use on the PinePhone, while noting low receive rates and untested modes; Wake-on-WLAN was not implemented at that time. See the [rtw88 RTL8723CS patch discussion](https://lwn.net/Articles/963985/). This is evidence of working hardware, not a claim of feature parity with the vendor driver.

For mainline, enable at least:

```text
CONFIG_CFG80211=y
CONFIG_MAC80211=y
CONFIG_RTW88=y
CONFIG_RTW88_CORE=y
CONFIG_RTW88_SDIO=y
CONFIG_RTW88_8703B=y
CONFIG_RTW88_8723CS=y
```

Install the current `linux-firmware` `rtw88/rtw8703b_fw.bin`; the optional WoW image does not create driver-side WoW support. The authoritative firmware directory is [`linux-firmware/rtw88`](https://gitlab.com/kernel-firmware/linux-firmware/-/tree/main/rtw88).

**Inference:** for a rebuilt vendor 4.4 kernel, retain the archived `8723cs.ko` only if its vermagic and symbol ABI exactly match the running build, or build the corresponding vendor source in-tree. Backporting modern rtw88 to 4.4 is a larger and less deterministic task. Do not substitute `rtl8723bs`: the B/BS and C/CS integrations are not interchangeable merely because the names are similar.

The board DTS must still describe the correct SDIO controller and power sequencing (`non-removable`, bus width, regulator/GPIO/clock, and usually `mmc-pwrseq-simple`). **Not established:** the exact TAQ-102 WLAN enable GPIO, 32.768-kHz clock, host-wake line, and SDIO controller instance should be copied from the measured vendor DT rather than guessed.

### Bluetooth

**Verified:** mainline knows RTL8723CS Bluetooth revisions. [`btrtl.c`](https://github.com/torvalds/linux/blob/master/drivers/bluetooth/btrtl.c) has CG, VF, and XX chip variants, with distinct RTL8723CS firmware/config names. The UART transport path is Realtek setup over H5/three-wire in [`hci_h5.c`](https://github.com/torvalds/linux/blob/master/drivers/bluetooth/hci_h5.c).

**Verified:** the current DT binding defines RTL8723CS Bluetooth as a serial-attached H5 device with `compatible = "realtek,rtl8723cs-bt"`; it also documents optional enable, device-wake, and host-wake GPIOs plus `max-speed`. See [`realtek,bluetooth.yaml`](https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/net/bluetooth/realtek,bluetooth.yaml). This Bluetooth function belongs under the correct UART node, independently of the Wi-Fi SDIO function.

Enable the relevant Bluetooth pieces:

```text
CONFIG_BT=y
CONFIG_BT_RFCOMM=y                  # only if the application needs it
CONFIG_BT_HCIUART=y
CONFIG_BT_HCIUART_3WIRE=y
CONFIG_BT_HCIUART_RTL=y
CONFIG_BT_RTL=y
CONFIG_SERIAL_DEV_BUS=y
```

**Verified:** the current [`linux-firmware/rtl_bt`](https://gitlab.com/kernel-firmware/linux-firmware/-/tree/main/rtl_bt) directory visibly carries the RTL8723CS **XX** firmware and config files, but not corresponding CG or VF files. A firmware merge request records that a known-working Armbian config was needed because the Realtek package did not include one: [linux-firmware MR 398](https://gitlab.com/kernel-firmware/linux-firmware/-/merge_requests/398/commits). Thus mainline driver recognition of CG/VF/XX does not guarantee that the distribution firmware bundle contains the matching pair for this tablet. Archive both the selected firmware and its matching config, not just a code blob.

**Not established:** which of CG/VF/XX this tablet reports, which UART and baud/flow-control wiring it uses, and whether its archived vendor files already have the exact raw format/names expected by `btrtl`. Determine the chip revision from stock Bluetooth logs or mainline `btrtl` probe output. Do not select `_xx` merely because it is larger. Copy the UART child-node GPIOs and wake polarity from the stock DT and inspect the stock vendor init command line.

## 5. Silead GSL3673 firmware and DT — complete

### The extracted firmware format is right

**Verified:** the mainline driver's on-disk format is an unheadered sequence of native `struct silead_fw_data { u32 offset; u32 val; }` records. It divides the file size by eight and issues one four-byte I2C write to each record's offset. See [`silead_ts_load_fw()`](https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/touchscreen/silead.c). It does not expect the 512-word `gsl_config_data_id_3673` array in that file.

**Corroborated:** the recovered `gsl3673.fw` is 4,719 × 8 = 37,752 bytes and has the classic page-select/write-pairs layout. That is exactly the regular format produced for the in-kernel `silead` driver by the community [gsl-firmware extraction tools](https://github.com/onitake/gsl-firmware#extracting-firmware). There is no record-count limit in the current load loop; 4,719 is therefore plausible. The file must retain little-endian 32-bit fields, which matches this ARMv7 device and the reported extraction.

**Safety check:** the current driver silently floors `size / 8`; it does not reject trailing bytes. Verify `size % 8 == 0`, parse every pair as little-endian, and compare a generated C representation record-for-record against the recovered vendor array before trusting the file.

With an explicit property such as:

```dts
firmware-name = "gsl3673.fw";
```

the driver requests:

```text
/lib/firmware/silead/gsl3673.fw
```

This prefix behavior is visible in [`silead_ts_read_props()`](https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/touchscreen/silead.c). Without an explicit name, the DT/I2C fallback derives a generic model name such as `silead/gsl3670.fw`; relying on that would obscure which panel calibration is installed.

### DT requirements and two upstream mismatches

**Verified:** the current DT binding requires `compatible`, `reg`, `interrupts`, `power-gpios`, `touchscreen-size-x`, and `touchscreen-size-y`. It also permits `firmware-name`, AVDD/VDDIO regulators, standard inversion/swap properties, `silead,max-fingers`, and the optional home button. See [`silead,gsl1680.yaml`](https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/input/touchscreen/silead,gsl1680.yaml) and the [common touchscreen binding](https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/input/touchscreen/touchscreen.yaml).

**Verified mismatch 1:** the binding and driver's OF table list GSL3670 and GSL3675 but not GSL3673. The measured vendor string `GSL,GSL3673` is also not a valid modern compatible due to spelling/case/vendor-prefix differences. For an upstream-quality solution, add and document `silead,gsl3673` in both the binding and OF table. For a local bring-up, using `silead,gsl3670` with the explicit GSL3673 firmware is a reasonable compatibility experiment because the driver has no per-model implementation data, but it remains an **inference until tested on this controller**. The relevant tables are in [`silead.c`](https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/touchscreen/silead.c).

**Verified mismatch 2:** although the binding permits `silead,max-fingers` up to five, the current driver does not read that property; it initializes ten slots and clamps reports at its constant `SILEAD_MAX_FINGERS = 10`. Thus adding `silead,max-fingers = <5>` will satisfy schema but will not alter current runtime behavior. This follows from the property reads and input setup in [`silead.c`](https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/touchscreen/silead.c).

**Inference:** use `touchscreen-size-x = <1024>` and `touchscreen-size-y = <600>` only if stock `getevent` measurements show the firmware already reports display-coordinate scale. Firmware calibration can produce a different raw maximum. Capture corner presses under stock Android and reproduce the measured inversion/swap/size mapping; do not derive axis orientation from portrait physical dimensions or panel timing.

**Verified:** this is not an ACPI-only path. The driver has a normal OF match table and the binding gives a DT example. Its UEFI/platform fallback is irrelevant when the explicit filesystem firmware exists. The driver needs a valid IRQ, AVDD/VDDIO regulators, and power GPIO behavior, and keeps the chip powered because power loss forgets firmware; those requirements are implemented in [`silead_ts_probe()`](https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/touchscreen/silead.c).

## 6. Mali-400 and rendering architecture — complete

### Recommendation

Use **pure KMS with CPU drawing for Phase A**, and make Lima an optional mainline optimization later. Do not make either the vendor Mali blob or OpenGL a boot-success dependency.

**Verified:** Lima is the upstream DRM driver for Mali-400/450 (`CONFIG_DRM_LIMA`), as stated in the kernel's [Lima Kconfig](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/lima/Kconfig). Mesa documents Mali-400 as supported, Rockchip KMS as a tested display-driver pairing, OpenGL ES 2.0 as the main target, and a high ES2 conformance pass rate in the [Lima documentation](https://docs.mesa3d.org/drivers/lima.html).

**Verified:** current upstream [`rk3128.dtsi`](https://github.com/torvalds/linux/blob/master/arch/arm/boot/dts/rockchip/rk3128.dtsi) includes an `rk3128-mali` / `arm,mali-400` GPU node, interrupts, clocks, OPPs, reset, and GPU power domain. The SoC-side Lima description is therefore already upstream; a board DTS needs to enable it and provide any board-level supply constraints.

**Verified limitation:** the GPU is a renderer, not the display controller. Mesa explicitly notes that a separate working display engine must share buffers with it. Lima therefore does nothing to solve the missing RK3126 LVDS output path. See [Mesa's display-driver explanation](https://docs.mesa3d.org/drivers/lima.html#display-drivers).

**Inference:** at 1024×600, one XRGB8888 framebuffer is about 2.34 MiB; double buffering is about 4.69 MiB. A dedicated appliance UI can comfortably allocate KMS dumb buffers, `mmap` them, and redraw dirty regions with a small raster library or its own routines. This keeps memory predictable and avoids the Mesa/LLVM footprint. Linux documents DRM/KMS as the primary-node modesetting interface and recommends corresponding libdrm wrappers in the [DRM userspace API](https://docs.kernel.org/gpu/drm-uapi.html).

Suggested escalation order:

1. `modetest` proves connector, mode, CRTC, plane, and page flipping.
2. A tiny libdrm dumb-buffer program paints a pattern and flips buffers.
3. Add input-event handling and CPU-rendered application UI.
4. On mainline only, enable Lima + Mesa and test `eglinfo`, `kmscube`, and the actual workload.

**Inference:** Lima is likely adequate for modest GLES2 transitions, charts, and compositing once mainline LVDS exists. It is unnecessary for a static dashboard and should not dictate architecture. On vendor 4.4, the recovered r7p0 kernel/user ABI and proprietary EGL/GLES libraries would have to match exactly; using those adds fragility with little benefit for the first milestone. LLVMpipe is the least attractive fallback on four Cortex-A7 cores and 1 GB: direct CPU rasterization without a GL abstraction is smaller and more deterministic.

## 7. Device-class traps — complete

1. **Partition names are not write coordinates.** The legacy parameter map, Rockchip's documented physical layout, and this board's measured ranges disagree at the low offsets. `rkdeveloptool wl` takes a sector number; its own [example states the unit explicitly](https://github.com/rockchip-linux/rkdeveloptool#readme). Resolve the physical LBA by a fresh `rl` plus full-image comparison, not by a partition label or generic table.

2. **The first loader is the recovery boundary.** Rockchip's `db` executes a temporary USB plug in RAM, but `ul` writes the ID block; the distinction is documented in [Rockusb](https://opensource.rock-chips.com/wiki_Rockusb). Do not run `ul`, erase-flash, low-level format, or any command that changes eMMC boot configuration. Locate a hardware mask-ROM recovery method before touching U-Boot or anything below it.

3. **Do not touch eMMC hardware boot partitions by analogy.** User-area loader offsets are the ordinary Rockchip path, while eMMC `boot0`/`boot1` selection is controlled separately. Read `EXT_CSD PARTITION_CONFIG` if curious, but leave it, `boot0`, `boot1`, and RPMB unchanged. Rockchip's [boot-flow documentation](https://opensource.rock-chips.com/wiki_Boot_option) describes the user-area chain.

4. **`trust` is part of the CPU/firmware contract, not Android clutter.** The stock `TOS ` image supplies the trusted ARMv7 stage in the miniloader flow. An RK3128 report demonstrates how a PSCI/trust mismatch can fault immediately at kernel start: [rockchip-linux/kernel issue 315](https://github.com/rockchip-linux/kernel/issues/315). This report is anecdotal, but it is a good example of why Phase A should preserve the known-working trust image.

5. **`misc` is small but stateful.** It carries Android's bootloader/recovery command structure, as defined by [AOSP](https://android.googlesource.com/platform/bootable/recovery/+/main/bootloader_message/include/bootloader_message/bootloader_message.h). Back it up immediately before recovery experiments, use the stock `adb reboot recovery` path first, and restore a stale recovery command if the device loops.

6. **The image topology may be Rockchip-specific.** This device has separate `KRNL`, `RSCE`, and `ANDROID!` objects. Rockchip's 4.4 documentation describes separate kernel, resource, and ramdisk-style images in [its RKIMG output](https://opensource.rock-chips.com/wiki_Rockchip_Kernel). Establish from stock U-Boot logs/commands whether the Android image's kernel field is used or whether U-Boot loads the `kernel` partition and DTB from `resource`. Repacking a perfectly valid Android image will achieve nothing if its changed component is not the one U-Boot consumes.

7. **Android 8 implies boot-header v0, but vendor verification is separate.** AOSP classifies Android 8 and older as boot image header v0 in its [boot image header documentation](https://source.android.com/docs/core/architecture/bootloader/boot-image-header). Preserve every recovered page size, address, board string, and command-line field. AVB absence would not prove Rockchip secure boot is disabled; only a controlled modified-image boot establishes acceptance.

8. **`resource.img` has structure and hashes.** The official Rockchip [`resource_tool`](https://github.com/rockchip-linux/u-boot/blob/next-dev/tools/rockchip/resource_tool.c) defines the `RSCE` header, 512-byte table blocks, `rk-kernel.dtb` path, and per-entry hashes. Keep the original resource image for the first milestone. When rebuilding it, pack with the same tool family, unpack it again, and byte-compare the embedded DTB; do not overwrite the first 0x800 bytes by hand.

9. **A flattened vendor DTB is not a universal hardware description.** It depends on vendor drivers, property spellings, clock IDs, regulator semantics, and display glue. Mainline needs a board DTS based on current bindings. Conversely, swapping a modern DTS into 4.4 can fail because numeric binding ABIs evolved. Validate DTS against the exact kernel tree that will consume it.

10. **PMIC support is not the same as tablet power support.** Mainline's RK8xx core includes RK816 regulator, RTC, GPIO, and power-key cells, but its RK816 cell list does not include a charger/fuel-gauge child; compare RK816 and RK817 in [`rk8xx-core.c`](https://raw.githubusercontent.com/torvalds/linux/master/drivers/mfd/rk8xx-core.c). The current [power-supply driver directory](https://github.com/torvalds/linux/tree/master/drivers/power/supply) has no RK816 battery driver. Mainline may therefore boot and regulate rails while lacking the vendor `rk816-battery` behavior, charge reporting, and policy. Treat charger/battery as a separate port and test charging thermals before unattended use.

11. **Regulator and GPIO mistakes can be electrical, not cosmetic.** Preserve the stock RK816 rail voltages, always-on rails, suspend states, panel/backlight polarity, Wi-Fi enables, and touch power/IRQ polarity until each consumer is identified. **Inference:** an incorrectly disabled DDR/logic rail, wrong PWM polarity, or wrong charger parameters can produce a hard brick-like failure or stress hardware. Change those nodes independently and test current draw/temperature.

12. **Old external modules require an exact kernel ABI.** A 2.4-MiB `8723cs.ko` is usable only with matching architecture, config-dependent symbol versions, and vermagic. Record `modinfo`, `uname -a`, `/proc/config.gz` if present, and symbol CRCs. Rebuilding “Linux 4.4” is not sufficient; the public [Rockchip `develop-4.4` branch](https://github.com/rockchip-linux/kernel/tree/develop-4.4) is not proven identical to this 4.4.103 vendor build.

13. **The public BSP is a starting point, not the corresponding source release.** Rockchip says its 4.4 tree supports RK312X and recommends `rockchip_linux_defconfig` in the [kernel wiki](https://opensource.rock-chips.com/wiki_Rockchip_Kernel), and the current branch list still exposes [`develop-4.4`](https://github.com/rockchip-linux/kernel/branches/all). Neither fact identifies the exact commit, downstream board patches, or compiler used for the Denver binary. Ask the vendor for the GPL source and preserve the recovered config before debugging public-tree differences.

14. **A visible boot logo does not prove Linux display works.** U-Boot can leave a framebuffer/backlight active, producing a stale logo even if the kernel VOP/LVDS probe fails. Require kernel DRM connector/mode state and successful page flips. Conversely, a black panel can be only backlight sequencing. Test VOP, LVDS, panel prepare, and PWM backlight as separate checkpoints.

15. **Touch firmware success is not calibration success.** The mainline driver considers status `0x5a5a5a5a` a successful load in [`silead.c`](https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/touchscreen/silead.c), but axis scale, inversion, swaps, edge clipping, and multi-touch tracking still need corner/grid measurements. Preserve the 512-word vendor config separately; mainline does not load it as a second file.

16. **Build on Linux and choose a 64-bit-time userspace.** Buildroot is designed for Linux hosts, according to its [system requirements](https://buildroot.org/downloads/manual/manual.html#requirement). For a long-lived 32-bit appliance, musl uses 64-bit `time_t`, while glibc requires Buildroot's `BR2_TIME_BITS_64`; the [Buildroot Y2038 section](https://buildroot.org/downloads/manual/manual.html#_how_does_buildroot_support_y2038) documents the choices. On macOS, use a Linux machine/container/VM rather than debugging unsupported host-tool behavior.

17. **Unattended appliance behavior needs explicit failure handling.** Do not immediately configure U-Boot or `init` to restart the application forever. During development, retain a console/recovery shell, persist a small boot/app failure counter in a non-critical writable location, and make the application fail visibly. Only add a watchdog after clean shutdown, battery-low behavior, charging, and recovery have been validated.

## 8. Concrete Phase A recipe — complete

This recipe deliberately defines Phase A as **stock early boot + stock kernel/DT first, Buildroot userspace, then a rebuilt vendor kernel**. Mainline U-Boot is moved behind the screen/touch milestone. Commands below are templates to run; none were executed on the tablet during this research.

### Gate 0 — recovery and observability before development

1. Open the tablet and identify the 3.3-V UART TX/RX/GND pads from board traces or continuity. Use a 3.3-V adapter, share ground, and do not connect the adapter's VCC. Determine baud rate from stock boot output and the recovered `chosen/stdout-path`; do not assume the common Rockchip rate.
2. Identify the mask-ROM test point/eMMC clock pad and document the exact physical recovery procedure while stock loader mode still works.
3. From unmodified Android, run `adb reboot recovery` once and prove that stock recovery boots and that normal Android can subsequently boot. This validates the intended experimental slot and the `misc` path before replacing anything.
4. Record `cat /proc/cmdline`, `cat /proc/version`, `uname -a`, `zcat /proc/config.gz` if available, `modinfo 8723cs`, the DT `/chosen` node, and stock `dmesg` for DRM, GSL3673, RK816, MMC, Wi-Fi, and Bluetooth.

**Stop condition:** do not perform a persistent write without either UART output or a proven USB-gadget shell path, known recovery-slot selection, and a documented mask-ROM fallback.

### Gate 1 — create a reproducible Linux build workspace

Use a Linux host. For a maintained appliance baseline, the current Buildroot download page lists `2025.02.x` as the LTS series through March 2028; as of this research the current LTS is 2025.02.17. See the [official download page](https://buildroot.org/download.html). Pin every source revision in the project manifest.

```text
git clone --branch 2025.02.17 --depth 1 https://gitlab.com/buildroot.org/buildroot.git buildroot
git clone https://android.googlesource.com/platform/system/tools/mkbootimg aosp-mkbootimg
git clone https://github.com/rockchip-linux/rkdeveloptool.git rkdeveloptool
git clone --branch develop-4.4 --single-branch https://github.com/rockchip-linux/kernel.git rockchip-kernel-4.4

git -C buildroot rev-parse HEAD
git -C aosp-mkbootimg rev-parse HEAD
git -C rkdeveloptool rev-parse HEAD
git -C rockchip-kernel-4.4 rev-parse HEAD
```

Keep a project-owned Buildroot `br2-external` tree with `configs/taq102_defconfig`, rootfs overlay, kernel config/fragments, application package, image scripts, and a manifest of artifact SHA-256 values. Buildroot documents that structure and out-of-tree invocation in its [br2-external manual](https://buildroot.org/downloads/manual/manual.html#outside-br-custom).

Copy working copies of the already-verified `boot.img`, `recovery.img`, `kernel.img`, `resource.img`, `misc.img`, DTB, and firmware into this workspace; leave the archive under `/Volumes` read-only. Verify the copied hashes against the recorded manifest before continuing.

### Gate 2 — map what stock U-Boot actually loads

Use AOSP's unpacker on both Android images. It can emit the exact `mkbootimg` reconstruction arguments; this behavior is documented in the [`unpack_bootimg.py` usage](https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/unpack_bootimg.py).

```text
python3 aosp-mkbootimg/unpack_bootimg.py \
  --boot_img stock/boot.img --out inspect/boot --format=mkbootimg \
  > inspect/boot.mkbootimg.args

python3 aosp-mkbootimg/unpack_bootimg.py \
  --boot_img stock/recovery.img --out inspect/recovery --format=mkbootimg \
  > inspect/recovery.mkbootimg.args

file inspect/boot/* inspect/recovery/*
sha256sum inspect/boot/* inspect/recovery/*
```

Then:

- Compare each unpacked kernel to the zImage extracted from the `KRNL` container.
- Inspect compression and list each ramdisk without modifying it.
- Record page size, load addresses, board, command line, header version, `second`, and DT fields.
- Run `avbtool info_image` if available and inspect U-Boot strings/logs for secure/verified-boot commands. A negative AVB result is only evidence that AVB metadata is absent, not that Rockchip image verification is absent.
- At the U-Boot console, capture `printenv` and the commands used by normal and recovery boot. Do not `saveenv`.

**Pass condition:** know whether recovery loads its own kernel, the separate `kernel` partition, and the DTB from `resource.img`. If this remains unknown, replace only the recovery ramdisk while retaining all stock kernel fields and stock resource.

### Gate 3 — build the smallest useful Buildroot initramfs

Start with userspace only. Configure:

- ARM little-endian, Cortex-A7, EABI hard-float with the matching VFP/NEON settings;
- musl for a small 32-bit time64 userspace;
- BusyBox init and a rescue shell/getty on the verified console;
- dynamic `/dev` with devtmpfs (and `mdev` if hotplug/module loading is needed);
- gzip-compressed CPIO root filesystem;
- `libdrm`, `libevdev`, `evtest`, and `libinput` only if the application needs it;
- Dropbear and USB Ethernet only after their kernel-side drivers are confirmed;
- no Mesa, LLVM, X11, Wayland, browser, or desktop in the first image.

Buildroot notes that devtmpfs requires `CONFIG_DEVTMPFS` and `CONFIG_DEVTMPFS_MOUNT`; because the kernel is initially external, verify or explicitly mount devtmpfs in early init. See [Buildroot `/dev` management](https://buildroot.org/downloads/manual/manual.html#_dev_management).

```text
make -C buildroot BR2_EXTERNAL="$PWD/taq102-br2" \
  O="$PWD/output-buildroot" taq102_defconfig
make -C buildroot O="$PWD/output-buildroot" menuconfig
make -C buildroot O="$PWD/output-buildroot" savedefconfig
make -C buildroot O="$PWD/output-buildroot"

file output-buildroot/images/rootfs.cpio.gz
gzip -dc output-buildroot/images/rootfs.cpio.gz | cpio -it
```

The initial init sequence should mount `/proc`, `/sys`, `/dev`, and `/run`; print a unique build ID to console and `/dev/kmsg`; bring up a shell; and only then attempt hardware tests. Do not launch the final application as PID 1 yet.

### Gate 4 — reproduce stock recovery, then change only its ramdisk

First reconstruct the stock image with AOSP `mkbootimg` and prove that unpacking the reconstruction yields identical metadata and payloads. Then copy `rootfs.cpio.gz` over only `inspect/recovery/ramdisk` and rebuild using the saved arguments.

```text
cp output-buildroot/images/rootfs.cpio.gz inspect/recovery/ramdisk

sh -c "python3 aosp-mkbootimg/mkbootimg.py \
  $(cat inspect/recovery.mkbootimg.args) \
  --output artifacts/recovery-buildroot.img"

python3 aosp-mkbootimg/unpack_bootimg.py \
  --boot_img artifacts/recovery-buildroot.img \
  --out inspect/recovery-buildroot --format=mkbootimg \
  > inspect/recovery-buildroot.mkbootimg.args
```

Compare `kernel`, `second`, and any DT/recovery-DT payload byte-for-byte against stock; only `ramdisk` and header hash/checksum consequences should differ. Confirm the image is no larger than the recovery partition. If stock U-Boot uses the separate `kernel` and `resource` partitions, leave both unchanged at this stage.

### Gate 5 — prove the exact physical LBA immediately before writing

Put the device in loader mode. In normal Loader mode, proceed directly with reads. Only when the device is in Maskrom mode and needs a temporary RAM loader should `db` be used, with the **same known-working RK312x loader binary used for the backup**. Never use `ul`. Confirm actual syntax with `rkdeveloptool -h`; the upstream [README](https://github.com/rockchip-linux/rkdeveloptool#readme) documents `db`, sector-addressed `wl`, and reset.

For each candidate recovery LBA, read the complete claimed range to a new local file and require a byte-for-byte match with the verified stock recovery image. Test both the measured board layout and any suspected parameter/physical shift, but write to neither until exactly one fresh read matches.

```text
rkdeveloptool ld
# Maskrom only, and only if required:
# rkdeveloptool db tools/KNOWN_WORKING_RK312X_LOADER.bin
rkdeveloptool rl CANDIDATE_RECOVERY_LBA RECOVERY_SECTOR_COUNT \
  verify/recovery-before.img
sha256sum verify/recovery-before.img stock/recovery.img
cmp verify/recovery-before.img stock/recovery.img
```

Set `RECOVERY_LBA` only from the successful read. Pad the new image only to a whole sector, check it fits, write it, and read back exactly what was written:

```text
cp artifacts/recovery-buildroot.img artifacts/recovery-write.img
IMAGE_BYTES=$(stat -c %s artifacts/recovery-write.img)
PADDED_BYTES=$(( (IMAGE_BYTES + 511) / 512 * 512 ))
truncate -s "$PADDED_BYTES" artifacts/recovery-write.img
WRITE_SECTORS=$(( PADDED_BYTES / 512 ))
test "$WRITE_SECTORS" -le "$RECOVERY_SECTOR_COUNT"

rkdeveloptool wl "$RECOVERY_LBA" artifacts/recovery-write.img
rkdeveloptool rl "$RECOVERY_LBA" "$WRITE_SECTORS" \
  verify/recovery-after.img
sha256sum artifacts/recovery-write.img verify/recovery-after.img
cmp artifacts/recovery-write.img verify/recovery-after.img
rkdeveloptool rd
```

**Pass condition:** write/readback hashes match and the device reaches the Buildroot shell through the already-proven recovery path. On rejection or no boot, restore the original recovery image to the same validated LBA and verify its readback before changing any other partition.

### Gate 6 — validate the stock kernel as a Linux-appliance kernel

At the Buildroot shell, capture:

```text
uname -a
cat /proc/cmdline
dmesg
find /dev/dri /sys/class/drm /sys/class/backlight -maxdepth 2 -print
modetest -M rockchip
evtest
cat /sys/class/power_supply/*/uevent
cat /proc/modules
```

Validation order:

1. UART or USB-gadget shell remains reliable across ten cold boots.
2. eMMC is detected but no production filesystem is mounted writable.
3. DRM reports the 1024×600 mode and repeated page flips work.
4. Backlight changes across safe mid-range levels without inverted behavior.
5. GSL3673 produces correctly mapped single-touch, corners, drag, then multi-touch.
6. RK816 readings remain plausible on battery and charger; clean poweroff and reboot work.
7. Only then load the matching `8723cs.ko`, verify its vermagic first, and configure networking.

Build a tiny libdrm dumb-buffer test next: select the connected connector/mode, create two XRGB8888 buffers, paint distinct patterns, modeset, and page-flip for several hours. Add `libevdev` touch handling after display stability. This isolates KMS/input from the final UI framework.

### Gate 7 — integrate the application without creating a boot loop

Package the application through the `br2-external` tree, not by copying into `output/target`; Buildroot warns that `target/` is not itself a deployable root filesystem in the [manual](https://buildroot.org/downloads/manual/manual.html#_buildroot_output). Run the app under a small supervisor after hardware initialization. On failure, log to kmsg and return to the rescue console. Keep a local gesture/key combination that prevents autostart.

Measure resident memory, page-flip latency, CPU use, thermal behavior, and 24-hour stability. Only after this gate should the application replace the rescue UI on normal boot.

### Gate 8 — rebuild the vendor kernel as a controlled substitution

First seek the exact Denver GPL source. If unavailable, use Rockchip `develop-4.4` as an explicitly approximate base; Rockchip's [kernel documentation](https://opensource.rock-chips.com/wiki_Rockchip_Kernel) confirms RK312X support and the ARM `rockchip_linux_defconfig` build path.

1. Recover the stock config from `/proc/config.gz` or `scripts/extract-ikconfig` and compare it to `rockchip_linux_defconfig`.
2. Use the compiler family recorded in `/proc/version`; old 4.4 BSP code may not build correctly with a current GCC without patches.
3. Add a standalone TAQ-102 vendor DTS derived from the recovered DTB and compile the recovered GSL3673 firmware/config headers into the matching vendor driver.
4. Keep DRM/VOP, RK3126 LVDS, panel, backlight, RK816, eMMC, and console built-in. Build Wi-Fi as a module only after symbol/version compatibility is under control.

```text
make -C rockchip-kernel-4.4 ARCH=arm \
  CROSS_COMPILE=arm-linux-gnueabihf- rockchip_linux_defconfig
make -C rockchip-kernel-4.4 ARCH=arm \
  CROSS_COMPILE=arm-linux-gnueabihf- menuconfig
make -C rockchip-kernel-4.4 -j"$(nproc)" ARCH=arm \
  CROSS_COMPILE=arm-linux-gnueabihf- zImage dtbs modules
```

Use the tree's `scripts/mkkrnlimg` for the `KRNL` object if stock U-Boot consumes it, and Rockchip `resource_tool` for `RSCE`. Its interface is implemented in the official [`resource_tool.c`](https://github.com/rockchip-linux/u-boot/blob/next-dev/tools/rockchip/resource_tool.c).

```text
rockchip-kernel-4.4/scripts/mkkrnlimg \
  rockchip-kernel-4.4/arch/arm/boot/zImage artifacts/kernel.img

resource_tool --pack --image=artifacts/resource.img rk-kernel.dtb
resource_tool --unpack --image=artifacts/resource.img verify/resource-unpacked
cmp rk-kernel.dtb verify/resource-unpacked/rk-kernel.dtb
```

Substitute only the kernel component in the already-working recovery boot path first. Re-run every Gate 6 check before touching the normal boot slot.

### Gate 9 — mainline and U-Boot are post-Phase-A work

For the mainline track, start with UART, eMMC, USB, RK816 core, and no display; then add the TAQ-102 DTS, Silead support, Wi-Fi/BT, and an RK3126 LVDS driver. Enable Lima only after KMS/LVDS works.

For a later U-Boot-proper experiment, retain stock ID block/miniloader and trust, build from `evb-rk3128_defconfig` plus a TAQ-102 U-Boot DT, and package `u-boot-dtb.bin` with Rockchip `loaderimage` at the verified `0x60000000` text base. The official [Rockchip U-Boot documentation](https://docs.u-boot-project.org/en/stable/board/rockchip/rockchip.html) and [`loaderimage` source](https://github.com/rockchip-linux/u-boot/blob/next-dev/tools/rockchip/loaderimage.c) describe this container step.

```text
make CROSS_COMPILE=arm-linux-gnueabihf- evb-rk3128_defconfig
make CROSS_COMPILE=arm-linux-gnueabihf- menuconfig
make -j"$(nproc)" CROSS_COMPILE=arm-linux-gnueabihf-

loaderimage --pack --uboot u-boot-dtb.bin artifacts/uboot.img 0x60000000
```

**Do not flash that image merely because it has `LOADER ` magic.** Require a correct U-Boot DT, stock-container comparison, UART, mask-ROM recovery, a freshly validated U-Boot physical LBA, and a known restoration command. This change does not advance the initial appliance milestone and should be scheduled only after the recovery and normal Linux images are stable.

## Bottom line

- Vendor-kernel-first is the right display/touch decision; vendor-U-Boot-first is not.
- Mainline RK3128 is materially healthier than expected: core SoC, RK3126 VOP, GPU/Lima, RTL8723CS Wi-Fi, and RTL8723CS Bluetooth support exist.
- The upstream blockers specific to this tablet are RK3126 LVDS, a board DTS, exact GSL3673 DT identification/calibration, and RK816 battery/charger behavior.
- The first image should reuse the stock boot chain, kernel, and resource DTB and replace only the recovery ramdisk with Buildroot. That obtains the desired non-Android appliance environment while preserving the most difficult working hardware integrations.

## Round 2 — corrections and answers

### Evidence status used in this section

- **Verified** means I inspected the cited source in this round, or the statement is a direct result of the device measurements supplied in `DELTA-ROUND2.md`.
- **Inferred** means the conclusion follows from those sources but has not yet been proved by compiling or booting on this TAQ-102.
- **Not independently verified** marks details that the web backend could not render. I do not use those details as premises below.

### 1. Gate 5: the dual-coordinate recommendation was wrong

**Round 1 was wrong to leave the recovery offset as an unresolved two-coordinate ambiguity after the device had already been measured.** The supplied measurements are much stronger evidence than the generic Rockchip table: all five recognizable partition headers occur at the parameter LBAs, and all 16 partition reads match slices of an independently read full-device image. For this particular eMMC, use parameter address = physical LBA.

There is no remaining reason to probe or write an alternative shifted recovery address. Gate 5 should use only physical LBA `196608` (`0x30000`) and a length of `131072` 512-byte sectors for the 64 MiB recovery partition. Testing the other coordinate system would now add risk rather than information.

Keep one read-before-write check, but change its purpose: it is a wrong-device, stale-shell-variable, and sector-count guard, not offset discovery.

```sh
RECOVERY_LBA=196608
RECOVERY_SECTOR_COUNT=131072

rkdeveloptool ld
rkdeveloptool rl "$RECOVERY_LBA" "$RECOVERY_SECTOR_COUNT" verify/recovery-before.img
sha256sum verify/recovery-before.img stock/recovery.img
cmp verify/recovery-before.img stock/recovery.img
```

Only if that full comparison succeeds should the recovery image be written and read back. Do not test the generic table's shifted LBA, and do not use `rkdeveloptool db` in Loader mode; the existing Round 1 rule that `db` is a Maskrom-only bootstrap remains unchanged. **Verified device fact:** the address decision comes from the 16/16 hardware comparisons reported in the delta, not from a web source.

### 2. The qop tree materially changes Gate 8

**Round 1's recommendation to fall back directly to `rockchip-linux/kernel` `develop-4.4` was wrong/incomplete.** The first public downstream base to evaluate should be [`54shady/qop_kernel`](https://github.com/54shady/qop_kernel). I directly rendered and inspected its named BND-D708 board source [`rk3126-bnd-d708.dts`](https://github.com/54shady/qop_kernel/blob/master/arch/arm/boot/dts/rk3126-bnd-d708.dts) and the GSL3673 driver [`gsl3673.c`](https://github.com/54shady/qop_kernel/blob/master/drivers/input/touchscreen/gsl3673.c). The latter includes `gsl3673.h`, calls `gsl_DataInit(gsl_config_data_id_3673)` in its no-ID path, and exposes that configuration array through its debug procedure. The tree also advertises the associated [`rk3126-bnd-d708.dtsi`](https://github.com/54shady/qop_kernel/blob/master/arch/arm/boot/dts/rk3126-bnd-d708.dtsi), [`gsl3673.h`](https://github.com/54shady/qop_kernel/blob/master/drivers/input/touchscreen/gsl3673.h), and Rockchip-era [`rk31xx_lvds.c`](https://github.com/54shady/qop_kernel/blob/master/drivers/video/rockchip/transmitter/rk31xx_lvds.c) paths, but GitHub did not render those three file bodies in this environment; verify them after cloning before taking their content as evidence.

I could not independently retrieve the qop Makefile/history pages in this run, so the exact `4.4.167` version and `2019-07-25` last-push date are **not independently verified here**. The one-day timestamp proximity must not be treated as provenance in any case. What is verified and decision-relevant is the exact board-family DTS plus the matching touch-driver implementation. This is therefore the **closest known public board-family BSP**, but it is not yet justified to call it the tablet's “near-exact source.” The exact Denver GPL release remains preferable if it can be obtained.

The MIPI-vs-LVDS divergence is useful. It supplies a controlled sibling configuration that helps separate:

- board-common wiring and supplies from display-variant wiring;
- BND/ODM choices from generic RK3126 integration;
- reusable UART, I2C, PMIC, touch, storage, and radio nodes from panel-specific nodes.

It is not permission to copy the sibling's display timing, touch bounds, Bluetooth UART, GPIOs, or panel sequence. Those are explicitly contradicted by the recovered tablet DT. The correct implementation is a new tablet-specific DTS, for example `rk3126-bnd-d708-taq102-lvds.dts`, which includes the most appropriate common files and substitutes the extracted LVDS panel timing, UART2 debug routing, and measured device nodes.

Revised Gate 8 sequence:

1. Pin an exact qop commit and archive its source hash. Confirm its Makefile version locally rather than relying on the repository summary.
2. Recover the stock kernel configuration and compiler identity (`/proc/config.gz`, if enabled, and `/proc/version`). If the qop Makefile confirms 4.4.167, it is still not ABI-identical to the stock 4.4.103 build.
3. Build the qop BND-D708 target unmodified first. This separates toolchain/configuration failures from TAQ-102 edits.
4. Diff the sibling DTS against the decompiled stock LVDS DT node-by-node. Classify every change as board-common, MIPI-only, or TAQ-102 LVDS-specific.
5. Add a new LVDS board DTS; do not overwrite the MIPI sibling DTS. Use the tablet's 51.2 MHz timing and recovered polarities/GPIOs, not the sibling's 49.5 MHz values.
6. Integrate the device-specific `GSL3673_FW` and `gsl_config_data_id_3673` arrays only in the vendor-kernel build. Build RTL8723CS in-tree or against the exact resulting ABI rather than reusing the stock external module blindly.
7. Substitute only the kernel component in the already-working recovery boot path first. Re-run every Gate 6 check before touching the normal boot slot.

**Inference:** this tree should reduce Phase-A board enablement from broad BSP archaeology to a bounded sibling-DTS port. It does not eliminate kernel-version, configuration, radio-module ABI, or LVDS-variant testing.

### 3. RK3126 LVDS: mostly integration now, not a blank-sheet driver

#### What I could and could not verify about the named U-Boot commit

The [`jcs/u-boot`](https://github.com/jcs/u-boot) repository is public and is a U-Boot fork. The exact commit URL is [`a5cb189fff6e9c6629adf8cebbe76893fc1f0f05`](https://github.com/jcs/u-boot/commit/a5cb189fff6e9c6629adf8cebbe76893fc1f0f05). However, GitHub's commit and patch renderers repeatedly returned cache misses in this research environment. Consequently, the claimed **274-line size, exact `rk3126_vop.c` diff, 40 kHz PWM value, and GPIO implementation are not independently verified in this run, and I do not rely on them**.

There is nevertheless stronger independently inspectable evidence for the central claim. Rockchip's 5.10 BSP [`rockchip_lvds.c`](https://raw.githubusercontent.com/rockchip-linux/kernel/develop-5.10/drivers/gpu/drm/rockchip/rockchip_lvds.c) already defines the RK3126 GRF register/masks, implements `rk3126_lvds_enable()` and `rk3126_lvds_disable()`, and matches `rockchip,rk3126-lvds`. The driver uses the DRM bridge/panel path and a generic PHY. A Linux DT-binding review explicitly discusses the manufacturer's `rockchip,rk3126-lvds` implementation and its GRF-parent topology, while noting that variants in the shared LVDS driver differ materially ([linux-arm-kernel review](https://lists.infradead.org/pipermail/linux-arm-kernel/2022-December/797439.html)). Rockchip's official RK3126 product page independently confirms that the SoC supports both LVDS and MIPI-DSI ([RK3126 product page](https://www.rock-chips.com/a/en/products/RK31_Series/2014/0924/523.html)).

There is an even closer current starting point: [`jcs/linux-dm250`](https://github.com/jcs/linux-dm250), described as a mainline Linux kernel for another RK3128 LVDS device, contains commit `1d5b0bbb` titled **“drm/rockchip: lvds: Support LVDS output on RK3126/RK3128.”** The indexed tree exposes that commit in the Rockchip DRM directory ([Tangled mirror](https://tangled.org/jcs.org/linux-dm250/tree/master/drivers/gpu/drm/rockchip)). Joshua Stein's hardware log documents the related RK3128 mainline/U-Boot work and the transition from no boot display to a later tree with video support ([DM250 development log](https://jcs.org/2025/03/14/dm250)); his current install guide points to his U-Boot tree “with video” and Linux tree containing the DTS sources ([DM250 install guide](https://jcs.org/2026/04/09/openbsd-dm250)). I verified the branch and commit title, but not the full `1d5b0bbb` patch body; fetch and review that commit before estimating or implementing from scratch.

**Correction to Round 1:** describing mainline RK3126 LVDS as a driver that still had to be written from the old BSP overstated the novel work. The essential register programming already exists in a modern Rockchip DRM driver, and a device-focused mainline branch now appears to contain the port. The work is primarily rebase, binding/PHY integration, board DTS, and hardware validation unless review of `1d5b0bbb` reveals gaps.

Realistic estimate, explicitly an **engineering inference** rather than a verified quotation:

- **Functional TAQ-102 prototype:** if `1d5b0bbb` applies cleanly and its PHY support matches this SoC revision, roughly 200–600 lines of TAQ-102 DTS/integration changes plus the existing patch, and about 3–10 focused engineer-days after UART/eMMC boot is stable.
- **If that patch does not contain a usable combo-PHY implementation:** roughly 400–800 changed/new driver and DTS lines and 1–3 focused engineer-weeks for a working prototype.
- **Upstream-quality series:** about 700–1,200 changed/new lines across binding, SoC DTSI, DRM/PHY code, and board DTS, with 3–8 weeks being more realistic once review, refactoring, suspend/resume, and more-than-one-board testing are included. Code line count is not the schedule driver; access to a stable UART console and repeatable panel measurements is.

Specific unknowns that must be resolved rather than transcribed blindly:

- whether RK3126C and RK3128 use identical combo DSI/LVDS PHY registers, GRF hiword masks, clocks, resets, and power-domain behavior;
- whether `1d5b0bbb` models the combo block as a generic PHY in the form current maintainers will accept, or embeds vendor sequencing in the bridge;
- the exact DT graph and binding needed for `mipi_lvds_phy` / `mipi_lvds_ctl` on current mainline;
- VOP route, output bus format (18/24-bit, JEIDA/SPWG), data order, clock/polarity, and panel enable/backlight sequencing for this physical panel;
- runtime-PM and suspend/resume behavior, including safe disable order;
- which parts are common enough to test on RK3128 as well as this RK3126C board.

Panel-enable GPIO and PWM backlight policy belong in generic panel/backlight drivers and DT where possible; they should not be copied wholesale from a bootloader display routine. The practical next step is to fetch `jcs/linux-dm250`, inspect `1d5b0bbb`, and diff it against both current mainline and Rockchip 5.10 before writing any new LVDS code.

### 4. Sibling-device research: useful firmware leads, no verified UART pinout

I verified that the Exo Wave i718 is sold as a 7-inch, 1024×600, 1 GB RK3126 tablet ([product listing](https://www.mercadolibre.com.ar/tablet-exo-wave-i718-1gb-8gb-7-android--selectogar/up/MLAU191056736)), and found a firmware-support thread that explicitly associates the Exo i718 with `bnd-rk3126-d708 a1.0` ([Servicell thread](https://www.servicell-arauca.com/foros/showthread.php?tid=29143)). Commercial firmware archives exist for the model ([Clancell archive](https://www.clancellsvip.com/index.php?a=downloads&b=folder&id=252)). These are actionable read-only leads for boot images, DTBs, boot arguments, and perhaps alternate kernel builds.

I did **not** find a credible teardown, UART test-pad map, or GPL source release for the Exo i718 or HCD T700B, and I did not independently establish the HCD T700B identity. Do not repeat that alias as verified. Commercial firmware archives are neither GPL source nor proof that an image is electrically compatible.

Recommended sibling chase:

1. Download candidate stock packages without flashing them; record hashes and unpack them offline.
2. Compare DT model/compatible strings, panel timings, GPIO assignments, kernel version/config, and U-Boot strings against the TAQ-102 extraction.
3. Search the resulting build strings and vendor identifiers for the original ODM GPL drop.
4. Ask Exo support and the importer for the corresponding GPL source, identifying the exact shipped firmware/kernel build.
5. Use teardown photographs only to generate UART candidates; verify voltage and ground with a meter before attaching an adapter.

### 5. The 512-word Silead configuration is not a DT calibration table

The qop vendor driver passes `gsl_config_data_id_3673` to `gsl_DataInit()` in its `GSL_NOID_VERSION` path and permits debug-time access to the array ([qop `gsl3673.c`](https://github.com/54shady/qop_kernel/blob/master/drivers/input/touchscreen/gsl3673.c)). That makes the 512 words input to the vendor “no-ID” tracking algorithm; it is not the controller firmware record stream and is not a standard set of touchscreen DT properties.

Mainline's [`drivers/input/touchscreen/silead.c`](https://github.com/torvalds/linux/blob/master/drivers/input/touchscreen/silead.c) loads firmware as eight-byte address/value records and obtains normal coordinate/orientation data through the generic touchscreen-property interface. It has no `gsl_DataInit()` consumer for the vendor array. Therefore, for the unmodified mainline driver, **the 512-word array goes nowhere and is simply unused**. Do not encode it as an invented DT property or append it to the `.fw` file.

The split is:

- DT: physical size/bounds, swap/invert axes, IRQ, reset/power GPIOs or regulators, and firmware filename using documented bindings.
- Mainline firmware file: the extracted controller firmware in the format expected by `silead.c`.
- Vendor-only array: retain it with the vendor driver and archive it for reverse engineering.

Test the mainline driver with the extracted firmware first. If coordinates and multitouch identity remain stable, the vendor no-ID array is unnecessary. If contacts jump, merge, or lose identity, coordinate calibration in DT or libinput will not repair the tracking algorithm. The choices are then to retain the vendor driver for Phase A or undertake a separate port/reimplementation of the no-ID algorithm and its config, which is likely difficult to upstream.

### 6. A connected battery makes missing RK816 charger policy relevant

The absence of an upstream RK816 charger/fuel-gauge driver is **not irrelevant** merely because the appliance is wall-powered. The RK816 support discussion states that charger/fuel-gauge work was deliberately outside the MFD series and that charging can auto-enable when a battery and external power are present; it also calls out future coordination of charger and OTG/boost behavior ([Linux kernel mailing-list archive](https://lists.openwall.net/linux-kernel/2024/05/03/290)). The RK816 datasheet documents programmable charge voltage/current, termination, timers, and related charger behavior ([RK816 datasheet v1.3](https://www.rockchip.fr/RK816%20datasheet%20V1.3.pdf)).

Thus a mainline kernel may boot while leaving charging governed by reset defaults or register state inherited from the stock bootloader, without an OS component that enforces the intended battery chemistry, current, temperature response, termination/recharge policy, fault reporting, or charger/boost coordination. A warm boot that happens to preserve safe stock settings is not a safety boundary. Permanent external power increases, rather than removes, the importance of validating that policy.

Practical disposition:

- **Vendor-kernel Phase A:** continue using the working stock/qop RK816 battery/charger integration; this is not a new blocker for the first appliance image.
- **Mainline with battery attached:** treat charger behavior as an acceptance blocker for unattended service. Measure charge voltage/current and temperature behavior through a complete charge/termination/recharge cycle, or port a suitable charger/fuel-gauge driver before deployment.
- **Battery removed/disconnected:** mainline can avoid the charging hazard, but first verify that the panel has a regulated, adequately sized supply and a clean shutdown strategy. Do not assume the PMIC path behaves identically without a battery.

### Round 2 decision summary

- Gate 5 now has one verified coordinate system: recovery starts at physical LBA `196608`. Keep a full stock comparison immediately before a write, but stop probing alternate coordinates.
- Gate 8 should start from a pinned qop board-family BSP commit, with generic Rockchip `develop-4.4` only as a comparative/fallback tree. The MIPI sibling is a useful diff control, not a source of TAQ-102 display values.
- Mainline RK3126 LVDS is not blank-sheet work. Inspect `jcs/linux-dm250` commit `1d5b0bbb` first, then compare it with Rockchip's verified 5.10 `rockchip,rk3126-lvds` implementation.
- The old jcs U-Boot commit's exact line count and implementation details were not independently rendered, so none of the plan depends on them.
- The 512-word GSL array stays vendor-side unless the no-ID algorithm itself is ported; it is not a DT property.
- A permanently connected battery makes missing charger policy a real safety concern for mainline deployment.
