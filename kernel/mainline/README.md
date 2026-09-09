# The TAQ-102 on mainline

**Start with `FINDINGS.md`**: what works, the two mainline bugs found, the
hypotheses that were wrong, and what is still open. This file is the build and
patch reference; `ISOLATING-THE-DISPLAY-HANG.md` is the narrative of the hunt.

Superseded in part: this file was written before any of it had run. It has now
run. The battery, the storage, the Wi-Fi, the USB console and the panel are all
proved on hardware -- see `FINDINGS.md`. Touch is the one subsystem still
untested. `boot` still holds the vendor appliance and every mainline image goes
to `recovery`, which remains the right arrangement.

Base: Linux 7.3.0-rc2, clone at `/work/linux-mainline` in the `taq102` VM,
built with the same Buildroot GCC 14.3 toolchain the vendor kernel uses. The
VM needs `libssl-dev` for `certs/extract-cert`.

## Why these three patches and no more

Mainline already carries most of this board. `rockchip,rk3126-vop` is in
`rockchip_vop_reg.c`; `phy-rockchip-inno-dsidphy.c` matches
`rockchip,rk3128-dsi-dphy` and already implements `PHY_MODE_LVDS`, which is
the same MIPI/LVDS/TTL combo PHY this panel hangs off. What is missing is the
wiring between the two, and it is small:

- **0001** gives `struct vop_output` an `lvds_en` and an `lvds_dclk_pol`, fills
  them in for RK312x, and uses them when the SoC has them. Upstream drives
  `DRM_MODE_CONNECTOR_LVDS` off the RGB enable, which is right for RK3288 and
  PX30 and wrong here: this VOP has its own LVDS bit. The values come from the
  vendor 4.4 tree, `rockchip_vop_reg.c:1150` -- `RK3036_AXI_BUS_CTRL` bit 26
  and bit 27, the same register upstream already uses for rgb, hdmi and mipi.
  SoCs without the bits keep the old path, chosen by `VOP_HAS_REG`.
- **0003** adds `rockchip,rk3126-lvds` to `rockchip_lvds.c` (0002 is its DT
  binding). The PX30 path is
  the model: the D-PHY does the work and the GRF only selects LVDS mode, the
  MSB order and the format. RK312x holds all of it in one register,
  `GRF_LVDS_CON0` at 0x150, with P2S_EN at bit 9, MODE_EN at 6, MSBSEL at 3
  and the format at 2:1 -- read off the vendor driver. There is no VOP
  selector because this SoC has one VOP.
- **0004** adds the `lvds` node to `rk3128.dtsi` and a third VOP output
  endpoint for it, shaped like the PX30 one.

`rk3126-taq102.dts` is the board. Every number in it comes from the vendor
board file or from the stock tree recovered off the tablet: the 1024x600
timing modetest measures at 56.14 Hz, the panel enable on GPIO2_B4, the
backlight on pwm0 at 25 us. `rk3128.dtsi` is the right base for an RK3126C
here -- its VOP compatible is already the rk3126 one.

## What is not done

- The RK816 fuel gauge. Mainline's `rk816s` MFD cells are pinctrl, clkout,
  regulator, pwrkey and rtc; there is no charger cell as there is for RK817,
  so `/sys/class/power_supply/battery` does not exist and the control centre's
  battery readout and the whole brightness policy have nothing to read.
  Scoped 2026-09-07: the register map is complete in the vendor
  `include/linux/mfd/rk808.h`, RK816 block -- charge state in `SUP_STS_REG`
  0xA0, current in `BAT_CUR_AVG` 0xBC, voltage in `BAT_VOL` 0xC4, the coulomb
  counter in `GASCNT` 0xB8..0xBB, full-charge count in `FCC_GASCNT` 0xD9, and
  the calibration pairs from 0xD2. The catch is that **0xE0 `SOC_REG` is a
  data register, not a hardware gauge**: the vendor computes the percentage in
  software and parks it there across reboots. So a faithful port means the
  coulomb counter, the OCV table and the calibration, which is the substance of
  the vendor's 5,000-line `drivers/power/rk816_battery.c`; a voltage-only
  driver would be a visible regression, since the percentage would move with
  load.
- The touch. `silead.c` knows gsl1680, 1688, 3670, 3675 and 3692, not the
  gsl3673 config array of `../patches/0005`.

## The PHY PLL: mainline is already on the vendor's divider pair

Answered 2026-09-08 by reading `drivers/phy/rockchip/phy-rockchip-inno-dsidphy.c`
at `28924df2a`. There is no shimmer to fix, because there is no arithmetic:
`inno_dsidphy_lvds_mode_enable()` hardcodes `prediv = 2, fbdiv = 28` and never
calls `inno_dsidphy_pll_calc_rate()`, which is reached only from
`inno_dsidphy_mipi_mode_enable()`. That is the 336 MHz pair `../patches/0001`
forces on the vendor kernel, bit for bit -- `REG_FBDIV_HI()` folds the `>> 8`
into the macro where the vendor driver passes `fbdiv >> 8` at the call site, so
the register writes are identical. The pixel clock never enters the LVDS path,
so 51.2 MHz changes nothing.

The 12/175 pair that shimmered is not reachable here even in principle. With
`ref` = `SCLK_MIPI_24M` the routine works from `fref = 24 / 2 = 12 MHz` and
bounds the prediv by `5 MHz < fref / prediv < 40 MHz`, giving
`min_prediv = 1`, `max_prediv = 2`. Asked for 358.4 MHz it would return 2/59
(354 MHz); asked for 51.2 MHz it would return 0, since both candidate fbdivs
fall under the 12 minimum and `pll.rate` is left untouched. A prediv of 12 is
out of range.

Patches `../patches/0002` and `../patches/0007` are a different matter: both
are missing. Mainline's LVDS path never powers on the common analog LDO and
PLL, never pulses `REG_SYNCRST` after loading the dividers, and never enables
the analog lanes behind the LVDS drivers, while `inno_dsidphy_power_off()`
powers the LDO and PLL down -- asymmetric, so a second power-on cannot recover.
And `inno->rst` is taken in probe under the name `apb` and then never used.
Ported as `0005` and `0006`; both `git apply --check` clean against
`28924df2a`, in that order.

Mainline needs nothing from `../patches/0001`'s clock hunks either. It has no
`host_base` and no `pclk_host` in probe, so it never writes a DSI host register
in LVDS mode -- no `DSI_PHY_STATUS` lock poll, and therefore no HCLK_VIO_H2P
requirement. It already enables `ref_clk` in `power_on`, which is the other
half of that patch.

One unverified difference: mainline adds `LOWFRE_EN_MASK` to the ANALOG 0x08
write and sets `PLL_OUTPUT_FREQUENCY_DIV_BY_1` (bit 5 clear); the vendor driver
leaves that bit at its reset value. If the panel comes up at half rate, that
bit is where to look.

## The series that actually builds (2026-09-08)

Built in `/work/linux-mainline` at `28924df2a` with the Buildroot GCC 14.3
toolchain: `zImage` 13234688 bytes, `rk3126-taq102.dtb` 27187 bytes, exit 0,
no compiler warnings. The tree at the end of that build reverse-applies all
four patches below as a set, so these files are exactly what compiled.

2026-09-10: 0018 (rtw88 SDIO shutdown powers the MAC off, so a warm reboot
does not wedge the RTL8723CS) sits on top of the seventeen below; v76 is the
seventeen plus 0018, zImage 10330704 bytes, otherwise the v75 DTB and ramdisk.
Untested until a cold boot: see the Wi-Fi item in `TODO.md`.

The series is `git am`-able as a whole, 0001 through 0017, each patch with a
message and a Signed-off-by: it is `git format-patch` output from a branch
built on `28924df2a` (2026-09-09, after the review recorded in `FINDINGS.md`).
DT bindings are their own patches, each ahead of the driver or DTS that
needs it. 0017 adds the board DTS whole, so `rk3126-taq102.dts` in this
directory is the same file, kept for reading. Checked: `git am` of the
seventeen onto `28924df2a` gives a tree equal to `/work/linux-mainline`
(`git diff` against the base identical, zero lines), `checkpatch --strict`
reports only its reminder that 0017 adds a file (the DTS directory is
already in the ARM/Rockchip MAINTAINERS entry), `dt_binding_check` passes on the
five bindings touched, and `dtbs_check` of the board DTB fails only on the
`panel-lvds` part number. v73 is that tree, running.

Numbering history, since the documents below use old numbers in places:
until 2026-09-09 the series was 0001-0007 and 0009-0012 with the config
fragment as 0008. The first renumbering that day inserted the LVDS binding
as 0002 and moved the board DTS to the end; the second split the remaining
bindings out and added the vendor prefix and board entry, giving seventeen.
Old to current: 0001 -> 0001, 0002 -> 0003, 0003 -> 0004, 0004 -> 0005,
0005 -> 0006, 0006 -> 0008 (binding 0007), 0007 -> 0010 (binding 0009),
0010 -> 0011, 0011 -> 0012, 0012 -> 0014 (binding 0013), and old 0009 (eMMC
and SDIO) lives inside 0017 with the vendor prefix as 0015 and the board
entry as 0016. The
first reconciliation, on 2026-09-08, had already deleted seven superseded
drafts after concurrent agents wrote three files numbered 0004 and two
numbered 0005; they are listed below by their names at the time.

then merge `taq102-mainline.config` onto the `.config` and run
`olddefconfig`. All 32 symbols the fragment names survive `olddefconfig`;
they were checked one by one against the resulting `.config`, not assumed.

**Do not apply these, they are superseded drafts of the four above and will
double-apply or conflict:**

- `0010-mfd-rk8xx-add-the-rk816-charger-cell.patch`,
  `0011-power-supply-rk816-battery-and-charger.patch`,
  `0012-dt-bindings-mfd-rockchip-rk816-charger-node.patch`,
  `0013-arm-dts-rk3126-taq102-rk816-battery-and-charger.patch`
  -- all four are inside `0004-power-supply-rk816-battery.patch`.
- `0006-input-silead-add-gsl3673.patch`,
  `0007-arm-dts-rk3126-taq102-gsl3673-touch.patch` -- both are inside
  `0005-input-touchscreen-gsl3673.patch`, which additionally puts the touch
  controller on i2c2 rather than i2c0 and drops an ACPI id this board has no
  use for.
- `0004-arm-configs-taq102-config-fragment.patch` -- superseded by
  `0007-taq102-mainline-config.fragment`. Its `blkdevparts` offset
  (`0x94480000`) and block device name (`mmcblk0`) are both wrong.

`NOTES-phy-pll-needs-no-patch.md` is a note, not a patch; there is nothing to
apply.

Two additions made at build time, both folded back into the files above so the
repository stays the source of truth:

- `CONFIG_CHARGER_RK816=y` was missing from the fragment, which was written
  before the driver existed. Without it `rk816_charger.c` is never compiled and
  `/sys/class/power_supply/battery` does not appear -- the exact failure the
  fragment's own comment warns about. Added, with the comment corrected.
- The DTS comment introducing the PMIC node claimed i2c0 carries the GSL3673.
  It does not; both the PMIC and the touch controller are on i2c2, and i2c0 is
  enabled with no children. Corrected in the tree and in
  `0004-power-supply-rk816-battery.patch`.

### Still missing from the board DTS

`&emmc`, `&sdmmc` and `&sdio` are all still `status = "disabled"` in the built
`rk3126-taq102.dtb`, inherited from `rk3128.dtsi`, and the board DTS has no
`mmc0/mmc1/mmc2` aliases. So this kernel boots with no storage and no SDIO,
which means no `/data` -- the `blkdevparts=mmcblk1:...` on the command line has
no device to attach to -- and no Wi-Fi, whatever `CONFIG_RTW88_8723CS` says.
The values (bus width, pinctrl groups, `cap-*`, the `mmc-pwrseq` for the
RTL8723CS and its 32.768 kHz clock from `rk816-clkout2`) are in the vendor
board file and were deliberately not guessed here.


## Reconciled, and the storage the series was missing (2026-09-08)

The four patches above build, but the build stage's own report said the result
would boot to a tablet with no `/data` and no Wi-Fi: `rk3128.dtsi` leaves the
eMMC, the SD and the SDIO controllers disabled and no lane enabled them.
`0009-arm-dts-rk3126-taq102-emmc-sdio.patch` closes that, with the stock
tree's own capabilities and the aliases that put the eMMC at `mmcblk1`, and
the tree still builds: zImage 13234688 bytes, DTB rebuilt and decompiled to
check both controllers really came up okay.

Deleted as superseded, having been folded into the files that build:
`0004-arm-configs-taq102-config-fragment.patch`,
`0006-input-silead-add-gsl3673.patch`,
`0007-arm-dts-rk3126-taq102-gsl3673-touch.patch`, `0010`, `0011`, `0012`,
`0013` and `gsl3673-fw-to-silead.py`.

One commit message was corrected rather than kept: the analog-power patch
claimed the equivalent vendor change "took the panel from lit-but-blank to a
correct image", which this repository's own README contradicts -- the panel is
correct with it and without it. The honest argument for carrying it is the
asymmetry in mainline's own driver, where `power_off` powers the common LDO
and PLL down and the LVDS enable never powers them back up.

### What still stands between this and the tablet

- **Userspace.** `glcube` links against the r7p0 Utgard blob, which cannot
  talk to `CONFIG_DRM_LIMA`. Nothing the appliance draws would appear. That is
  a Buildroot change -- Mesa with the lima gallium driver -- and it is the
  largest remaining piece.
- **Firmware.** `silead/gsl3673.fw` and `rtw88/rtw8703b_fw.bin` are requested
  asynchronously; missing, they give a silently absent touchscreen and a
  silently absent `wlan0`, not a probe error. Both have to be in the initramfs.
- **Nothing has run on hardware.** The parts a compiler cannot check are
  exactly the parts most likely to be wrong: the LVDS bring-up, the coulomb
  counter, the touch controller's unaided output.
- **Shutdown policy.** With the vendor's zero algorithm gone, a shutdown must
  be triggered on voltage, not on the reported percentage.

## Four boots, no output, and what each one eliminated (2026-09-08)

None of these reached a shell. They are recorded because each one closed a
door, and because the cost of a failed attempt here is a physical recovery by
hand: the board's only console is a USB gadget that userspace raises, so a
kernel that dies before that is completely silent, and the way back is the
two-button loader-mode dance, which this project has always found unreliable.

**v50** -- the first mainline image. Black screen, no USB, no network.

**v51** -- `multi_v7_defconfig` builds 163 ARM platforms into the kernel;
disabling every non-Rockchip `ARCH_*` took the decompressed Image from 33.7 MB
to 25.7 MB. The theory was that it overwrote the ramdisk. It did not: measured
from the running vendor kernel, `/sys/kernel/debug/memblock/reserved` puts the
FDT at 0x64600000 and the initrd at 0x64bf0000, while our Image ends at
0x619C6F1C. Still black. **Kernel size eliminated.**

**v52** -- the memory node was wrong and this was a real bug: the vendor
kernel reports two banks, `60000000-683fffff` and `69200000-9fffffff`, with a
14 MB hole between them, and our DTS declared one contiguous gigabyte. Fixed.
Still black, so it was not the cause either, but it had to be right.

**v53/v54** -- the diagnostic build: `CONFIG_USB_G_SERIAL` so the *kernel*
owns the ACM gadget and can print before userspace exists, `nosmp`,
`CONFIG_CPU_FREQ` off, ramoops rewired to the form mainline actually parses,
and `&display_subsystem` enabled -- `rk3128.dtsi` leaves it disabled and no
board file here had ever overridden it, so the Rockchip DRM master never bound
at all. **The screen came up white instead of black.**

That white screen is the most useful result of the day. Black meant the kernel
died before anything touched the panel. White means the panel is powered and
lit, which needs a driver bound or `/init` running -- so mainline gets as far
as the driver phase, and possibly into userspace. Every early-boot theory dies
with it, and so do the two leading candidates from an independent review
(a PSCI-versus-`rockchip,rk3036-smp` firmware contract mismatch, and cpufreq
scaling with no CPU regulator), because this build had `nosmp` and no cpufreq.

**What that leaves.** The kernel reaches the driver phase and still enumerates
nothing on USB -- not the gadget, not even a charging device. So the suspect is
now DWC2 or its PHY, which is an ordinary driver failure with an ordinary log.
This build writes that log to ramoops at 0x68100000, and it is still sitting in
RAM. Reading it back is the next step, and it is a race: 0x68100000 is ordinary
RAM to the vendor kernel, so the dump has to happen immediately after recovery,
before anything allocates over it.

### Hypotheses eliminated by reading the source, not by burning a boot

- **The `MZ` header is not an EFI requirement.**
  `arch/arm/boot/compressed/efi-header.S` emits two `eor r5, r5, #0x4d000`
  instructions that cancel each other; the legacy entry at +0x20 still works.
  (`CONFIG_EFI_STUB` was disabled anyway, and the `EEEE` word at 0x34 is
  zImage extension metadata that stays either way.)
- **The machine compatible does not matter.** `mach-rockchip/rockchip.c` lists
  neither rk3126 nor rk3128, but `arch/arm/kernel/devtree.c` falls back to a
  GENERIC_DT descriptor, and `arch/arm/kernel/time.c` then calls `of_clk_init()`
  and `timer_probe()` itself. The in-tree `rk3128-evb.dts` has the same
  property.
- **`CLK_RK312X` was already enabled.** An earlier note in this repository
  claimed it was missing; that was a check for `CLK_RK3128`, which is not a
  symbol. The driver object was in the tree before the first attempt.
- **The container is fine.** The known-good vendor image (v49) was packed the
  same way, written to `recovery`, and booted normally -- so mkbootimg layout,
  the RSCE resource image, the recovery partition and the BCB mechanism are all
  proven, and none of them should be re-litigated.


## Why the RAM mailbox cannot work with this recovery path (2026-09-08)

The mailbox was built, proved, and then defeated by the recovery procedure
itself.

Proved: `tools/dtb-add-memreserve.py` puts 0x68100000 in the vendor DTB's
reserve map without recompiling the blob, v58 boots with
`0x68100000..0x681effff` in its reserved list, and a synthetic ramoops zone
written there survived a **reboot** and was rescued by `/init` into `/data`
with its signature and text intact.

Defeated: the only way back from a failed mainline boot is the button dance,
and that dance holds power until the PMIC drops the rails. DRAM loses its
contents. After the sixth mainline attempt the region held uniform random
bytes -- the signature of freshly powered DRAM, not of an overwritten log --
so nothing can be concluded about whether mainline ever wrote there.

The synthetic test passed because `reboot` is a warm reset that never cuts
power. That difference is the whole result, and it was not obvious until the
two runs were compared.

**So any diagnostic channel that lives in RAM is useless here**, including the
assembly stage-writer idea, unless a recovery path is found that does not
remove power. Entering loader mode without a power cycle has happened once,
by accident, and is not reproducible on demand.

### The channel that does survive: the bootloader's own framebuffer

The vendor command line names it: `uboot_logo=0x02000000@0x9dc00000`. U-Boot
leaves a 32 MB framebuffer at 0x9dc00000 with the panel lit and scanning it
out -- which is what the white screen has been all along. A
`simple-framebuffer` node pointing at that address, with `CONFIG_FB_SIMPLE`
and fbcon, gives the kernel a console on the panel from very early in boot,
with no USB, no DRM driver, no timer, and nothing that has to survive a power
cycle: the text is simply on the screen, to be read or photographed.

That is the next thing to try, and it is the first proposed channel whose
failure mode is also informative -- if no text appears, the kernel is dying
before fbcon registers, which is earlier than anything reached so far.


## Linux 7.3.0-rc2 boots on the TAQ-102 (2026-09-08)

v59, and the console that finally answered was the one the bootloader had been
handing us all along.

`uboot_logo=0x02000000@0x9dc00000` on the vendor command line is a framebuffer
U-Boot leaves lit and scanning out -- the white screen this hunt had been
staring at for six attempts. A `simple-framebuffer` node pointing at it, with
`CONFIG_FB_SIMPLE` and fbcon, and `CONFIG_DRM_ROCKCHIP` **off** so nothing
takes the panel away from that buffer, put printk on the screen from early
boot. And with the panel no longer contested, the USB gadget came up too: the
ACM console enumerated for the first time on mainline and the whole log came
out over the cable.

What the log shows, running:

- `arch_timer: cp15 timer running at 24.00MHz`, both memory banks correct
- **the RK816 driver we wrote works**: `capacity=100`, `status=Full`,
  `voltage_now=4153600`, `current_now=-3012`, and `usb/online` = 1
- eMMC at `mmcblk1` with the `blkdevparts=` partition, `/data` mounted ext4
  after a journal recovery
- `rtw88_8723cs` bound, wlan0 authenticated, associated, DHCP lease, mdnsd
  announcing `taq102.local`
- the backlight beacon reporting **code 4**: a UDC exists, a gadget is bound,
  and the host has configured it

The one failure is self-inflicted and expected: `glcube` cannot open
`/dev/dri/card0` because this diagnostic build has the Rockchip DRM disabled
on purpose. The panel and the KMS driver are the next build, not a defect.

Console log: `docs/evidence/2026-09-08-mainline/first-mainline-boot-console.log`.

### Why six attempts produced nothing

Every diagnostic channel tried before this one depended on something that was
not there. The USB gadget needs userspace, or so it seemed, and userspace was
never reached. ramoops lives in RAM, and the only recovery from a failed boot
holds power until the PMIC drops the rails, so the RAM was gone every time.
The backlight beacon runs near the end of `/init`. The bootloader's own
framebuffer needed none of them: it is already there, already lit, and it
survives because the bootloader put it there before the kernel ran at all.

## The image recipe that runs the cube (2026-09-09)

Three inputs, all in `/Volumes/Datos4TB2/denver-taq102/gate3-build/`:

- kernel: `zImage-7.3.0-rc2-variant-M` -- the VM tree at
  `/work/linux-mainline` with the series applied and `.config` as
  `taq102-mainline.config` describes it (PHY and Lima `=m`, everything else
  in); v69 is the same with the reviewed fixes (`zImage-7.3.0-rc2-v69`);
- resource: `tools/make-resource.py <stock second> rk3126-taq102-v65.dtb` --
  the DTB from this directory's `rk3126-taq102.dts`, which is the VM's file
  copied out (simple-framebuffer, reboot-mode, the LVDS endpoint, `&gpu`);
- ramdisk: `rootfs-v66-cube-auto.cpio.gz` -- Buildroot's
  `taq102_mainline_defconfig` output with the four modules copied into
  `/lib/modules/$(uname -r)/`, `/usr/sbin/taq102-cube`, and inittab running
  `taq102-cube app`.

Then `KERNEL=... SECOND=... MKBOOTIMG=tools/vendor/mkbootimg.py
sh tools/make-recovery.sh <ramdisk> <out.img>` and `tools/flash-recovery.sh`.
From a running mainline image `/usr/sbin/reboot-loader` drops the tablet into
loader mode, so no buttons are needed between images; only a hung one costs
I the dance, with `tools/loader-watch.sh recovery <img>` armed first.
