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
- The touch. `silead.c` knows gsl1680, 1688, 3670, 3675 and 3692, not the
  gsl3673 config array of `../patches/0005`.
- Whether mainline's PHY PLL picks the same divider pair that patch
  `../patches/0001` needed here. That is the shimmer, and it is measured with
  the panel camera, not read.
