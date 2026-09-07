# The TAQ-102 on mainline

Work in progress. Nothing here has run on the tablet yet, and nothing goes
near `boot` until the panel, the touch, the battery and the Wi-Fi are all four
proved. The journal entry is in `../../README.md`; the backlog item is in
`../../TODO.md`.

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
- **0002** adds `rockchip,rk3126-lvds` to `rockchip_lvds.c`. The PX30 path is
  the model: the D-PHY does the work and the GRF only selects LVDS mode, the
  MSB order and the format. RK312x holds all of it in one register,
  `GRF_LVDS_CON0` at 0x150, with P2S_EN at bit 9, MODE_EN at 6, MSBSEL at 3
  and the format at 2:1 -- read off the vendor driver. There is no VOP
  selector because this SoC has one VOP.
- **0003** adds the `lvds` node to `rk3128.dtsi` and a third VOP output
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
Ported as `0004` and `0005`; both `git apply --check` clean against
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

Apply in order, 0001 through 0009. The numbering was reconciled by hand after
the build: concurrent agents had written three files numbered 0004 and two
numbered 0005, and the seven superseded drafts listed below were deleted
rather than left to be applied by mistake.

then merge `0008-taq102-mainline.config` onto the `.config` and run
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
