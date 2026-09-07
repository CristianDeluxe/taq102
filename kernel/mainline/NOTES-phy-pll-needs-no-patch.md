# Mainline needs no PLL patch for this panel

Read against `/work/linux-mainline` at `28924df2a`,
`drivers/phy/rockchip/phy-rockchip-inno-dsidphy.c`, and the vendor 4.4 driver
`/work/kernel/drivers/phy/rockchip/phy-rockchip-inno-video-combo-phy.c` with
`kernel/patches/0001`, `0002` and `0007` applied on top of it.

There is no `0006-...-pll.patch`. Mainline already programs exactly the divider
pair this panel was measured to want, and every plausible edit to that code path
would move it off that pair. The two things mainline really is missing are the
analog power-up and the reset pulse, which are
`0004-phy-rockchip-inno-dsidphy-lvds-common-analog-power.patch` and
`0005-phy-rockchip-inno-dsidphy-pulse-reset-at-power-on.patch`.

## 1. The LVDS path does not run the PLL routine at all

`inno_dsidphy_lvds_mode_enable()` (line 600) opens with

    u8 prediv = 2;
    u16 fbdiv = 28;

and writes them straight into `REGISTER_PART_ANALOG` 0x03/0x04. The pixel clock
is never an input. `inno_dsidphy_pll_calc_rate()` is defined at line 336 and has
exactly one caller, line 419, inside `inno_dsidphy_mipi_mode_enable()`:

    $ grep -n inno_dsidphy_pll_calc_rate drivers/phy/rockchip/phy-rockchip-inno-dsidphy.c
    336:static unsigned long inno_dsidphy_pll_calc_rate(struct inno_dsidphy *inno,
    419:	inno_dsidphy_pll_calc_rate(inno, cfg->hs_clk_rate);

So there is no rate-dependent PLL selection in LVDS mode to fix.

## 2. 2/28 is bit-for-bit the vendor's measured pair

The vendor driver's LVDS function hardcodes the same two numbers; `patches/0001`
only adds the comment recording the camera measurement (2/28 holds a
single-pixel line pattern to 0.005 px rms, 12/175 wobbles it 0.63-0.81 px rms).

With `ref` = `SCLK_MIPI_24M` (24 MHz, from the `dphy` node in `rk3128.dtsi`,
lines 651-661):

    24 MHz / prediv 2 * fbdiv 28 = 336 MHz serial clock

The register writes are identical too, despite differing macros:

| | vendor | mainline |
|---|---|---|
| `REG_FBDIV_HI(x)` | `UPDATE(x, 5, 5)`, called as `REG_FBDIV_HI(fbdiv >> 8)` | `UPDATE((x >> 8), 5, 5)`, called as `REG_FBDIV_HI(fbdiv)` |
| value for fbdiv 28 | `28 >> 8` = 0 -> bit 5 clear | `28 >> 8` = 0 -> bit 5 clear |

`REG_PREDIV(x)` and `REG_FBDIV_LO(x)` are the same macro in both trees.

## 3. What calling the routine would produce instead

The panel is 51.2 MHz pixel clock (`clock-frequency = <51200000>` in
`rk3126-taq102.dts`), 1024x600 at 56.14 Hz. `inno_dsidphy_pll_calc_rate()` works
in half-rate units: `fref = prate / 2` = 12 MHz, and it bounds prediv by
5 MHz < fref/prediv < 40 MHz, so

    min_prediv = DIV_ROUND_UP(12e6, 40e6) = 1
    max_prediv = 12e6 / 5e6              = 2

Three candidate targets, all worse than the constant:

- **168 MHz** (the routine's own units for the good 336 MHz output). prediv 1
  gives `fbdiv = 168/12 = 14`, `14 * 12 / 1 = 168 MHz`, delta 0, and the loop
  `break`s immediately. Result **1/14**: the right output frequency but a
  divider pair this panel has never been run on, on a board where the divider
  pair is precisely what was shown to matter.
- **358.4 MHz** (7x pixel clock, the serialisation rate). prediv 1 gives
  fbdiv 29 -> 348 MHz (delta 10.4 MHz); prediv 2 gives fbdiv 59 -> 354 MHz
  (delta 4.4 MHz). Result **2/59**, i.e. 708 MHz serial: wrong by roughly 2x.
- **51.2 MHz** (the pixel clock itself). prediv 1 -> fbdiv 4, prediv 2 ->
  fbdiv 8; both below the hardware minimum of 12, so both `continue`.
  `best_freq` stays 0, the `if (best_freq)` block never runs, and
  `inno->pll.prediv`/`fbdiv` keep their `devm_kzalloc` zeroes. That writes
  prediv 0 into the divider: a blank panel.

The 12/175 pair that shimmered is not reachable from this routine either -
`max_prediv` is 2.

## 4. The other hunks of vendor `patches/0001` do not apply to mainline

- `pclk_host` / `host_base`: mainline's probe maps one resource
  (`devm_platform_ioremap_resource(pdev, 0)`) and gets two clocks, `ref` and
  `pclk`. There is no DSI-host mapping and no `host_base` member, so there is
  nothing to clock. Confirmed by grep: neither identifier exists in the file.
- `h2p_clk` (HCLK_VIO_H2P): needed on the vendor driver only because its LVDS
  path pokes the DSI host block - `readl_relaxed_poll_timeout(inno->host_base +
  DSI_PHY_STATUS, ...)`. Mainline writes no DSI host register in LVDS mode, so
  the gated-clock hang that patch avoids cannot happen.
- `clk_prepare_enable(inno->ref_clk)`: mainline's `inno_dsidphy_power_on()`
  already does this (line 657), which was the other half of the vendor patch.

## 5. One difference left deliberately unpatched

Mainline's first LVDS write masks in `LOWFRE_EN_MASK` and sets
`PLL_OUTPUT_FREQUENCY_DIV_BY_1` (bit 5 of ANALOG 0x08 cleared); the vendor masks
only `SAMPLE_CLOCK_DIRECTION_MASK` and leaves bit 5 at whatever the hardware
reset left. If the reset default of that bit is 0, mainline's write is a no-op
and the two drivers are identical. If it is 1, the vendor is running the output
divided by two and mainline is not.

Nothing in either tree says which, and after `0005` pulses the APB reset the
register is at its reset default in both cases. Changing it would be a guess
about a rate-halving bit, on a board whose history is that every guessed value
cost a day, and it would change behaviour for every other SoC this driver serves
(px30, rk3568, rv1126, rk3506, rk3368). Left alone; if the bring-up comes up at
half or double rate, this bit is the first thing to look at.

Also worth knowing during bring-up: mainline has no `PHY_LOCK` poll in LVDS mode,
so a PLL that fails to lock gives a blank panel and an empty dmesg - the vendor's
"PLL is not lock" diagnostic does not exist here.
