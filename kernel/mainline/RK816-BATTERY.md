# RK816 battery and charger on mainline

What mainline is missing, where every number comes from, and what the four
patches `0010`–`0013` in this directory actually do.

Sources read: `/work/kernel/drivers/power/rk816_battery.c` (4998 lines) and
`rk816_battery.h`, `/work/kernel/include/linux/mfd/rk808.h`, the vendor board
file `/work/kernel/arch/arm/boot/dts/rk3126-taq102.dts`, and in
`/work/linux-mainline` the files `drivers/mfd/rk8xx-core.c`,
`drivers/mfd/rk8xx-i2c.c`, `drivers/power/supply/rk817_charger.c` and
`include/linux/mfd/rk808.h`.

## 1. Which registers give what

All i2c registers on the PMIC at `i2c2` address `0x1a`. Names are mainline's
`include/linux/mfd/rk808.h`, which already carries the whole RK816 fuel gauge
block even though no driver used it.

| what | register | how it is read |
| --- | --- | --- |
| charge state machine | `RK816_SUP_STS_REG` 0xa0, bits 6:4 | 0 off, 1 dead, 2 trickle, 3 CC/CV, 4 finish, 5 USB over-volt, 6 battery temp error, 7 timer error |
| battery present | `RK816_SUP_STS_REG` 0xa0, bit 7 | `BAT_EXS` |
| VBUS present | `RK808_VB_MON_REG` 0x21, bit 6 | `PLUG_IN_STS` |
| average current | `RK816_BAT_CUR_AVG_REGH/L` 0xbc/0xbd | big-endian, 12-bit two's complement, `uA = adc * 1506` at 20 mΩ |
| average voltage | `RK816_BAT_VOL_REGH/L` 0xc4/0xc5 | `mV = (k*adc/1000 + b) * 1100/1000` |
| open circuit voltage | `RK816_BAT_OCV_REGH/L` 0xc2/0xc3 | same scaling; sampled every 8 min per `GGCON` bits 3:2 |
| voltage calibration | `RK816_VCALIB0_REGH` 0xd5, `RK816_VCALIB1_REGH` 0xd7 | factory points at 3.0 V and 4.2 V; `k = 1200000/(v1-v0)`, `b = 4200 - k*v1/1000` |
| coulomb counter | `RK816_GASCNT_REG(3..0)` 0xb8–0xbb | 32-bit big-endian, `mAh = count / 2390` at 20 mΩ |
| coulomb preset | `RK816_GASCNT_CAL_REG(3..0)` 0xb4–0xb7 | write to seed the counter |
| latched full-charge count | `RK816_FCC_GASCNT_REG(3..0)` 0xd9–0xdc | valid only while `GGSTS` bit 5 `FCC_LOCK` is set |
| relaxed voltage / current | 0xca–0xcd, 0xce–0xd1 | valid only while `GGSTS` bits 3:2 are both set |
| minutes powered off | `RK816_NON_ACT_TIMER_CNT_REG` 0xd4 | hardware minute counter, the RK816's equivalent of the RK817's `OFF_CNT` |
| first-connect flag | `RK816_GGSTS_REG` 0xb1, bit 4 | `BAT_CON`, cleared by software |
| charge enable / V / I | `RK816_CHRG_CTRL(0)` 0xa3 | bit 7 enable, 6:4 voltage index, 3:0 current index |
| termination current | `RK816_CHRG_CTRL(1)` 0xa4, bits 7:6 | 100/150/200/250 mA at 20 mΩ |
| termination mode, CCCV timer | `RK816_CHRG_CTRL(2)` 0xa5 | bit 5 digital termination, bit 2 CCCV timer (vendor clears it) |
| input current limit | `RK816_USB_CTRL_REG` 0xa1, bits 3:0 | 450, 80, 850, 1000, 1250, 1500, 1750, 2000 mA |
| input V/I limiters | `RK816_SUP_STS_REG` 0xa0, bits 3 and 2 | `USB_VLIMIT_EN`, `USB_CLIMIT_EN` |
| gauge enable | `RK816_TS_CTRL_REG` 0xac, bits 7 and 6 | `GG_EN`, `ADC_CUR_EN` |
| input to system | `RK816_BAT_CTRL_REG` 0xa6, bit 6 | `USB_SYS_EN` |

### USB and AC presence: what the hardware can and cannot say

`rk816_battery.h` defines `USB_EXIST` (0xa0 bit 1) and `USB_EFF` (bit 0), and
the vendor driver **never reads them**. The only place those two names appear
in 5000 lines is as `case` labels at `rk816_battery.c:2109` and `:2112`, inside
a switch on `status & CHRG_STATUS_MSK` where the mask is `0x70` — so bits 1
and 0 can never reach them. They are dead code.

What the vendor actually trusts is `PLUG_IN_STS`, `RK808_VB_MON_REG` bit 6
(`rk816_battery.c:1586`, `:1683`, `:1701`, `:3405`, `:3525`). That is the
authoritative "there is VBUS" bit, and at `:3399` `rk816_bat_check_charger()`
uses it to override the USB PHY in both directions.

Nothing on the chip distinguishes a wall adapter from a laptop port.
`di->usb_in` and `di->ac_in` (`:1201`, `:1234`) are set only from extcon events
delivered by the SoC USB PHY's BC1.2 detection (`:1509`
`rk816_bat_set_chrg_param`, `:3816` the extcon worker), plus `di->dc_in` from a
DC barrel jack this board does not have — its vendor node sets
`dc_det_adc = <0>` and declares no `dc_det_gpio`, so the driver prints "not
support dc".

## 2. How the state of charge is actually produced

**Confirmed: `RK816_SOC_REG` at 0xE0 is not a gauge.** It is written in exactly
one function and read in exactly one function:

```c
static void rk816_bat_save_dsoc(struct rk816_battery *di, u8 save_soc)
{
	static int last_soc = -1;

	if (last_soc != save_soc) {
		rk816_bat_write(di, RK816_SOC_REG, save_soc);   /* :1438 */
		last_soc = save_soc;
	}
}

static int rk816_bat_get_prev_dsoc(struct rk816_battery *di)
{
	return rk816_bat_read(di, RK816_SOC_REG);              /* :1445 */
}
```

The value written is `di->dsoc`, the driver's own computed display percentage.
Mainline's `include/linux/mfd/rk808.h` reaches the same conclusion
independently: it names the block `RK816_DATA_REG(x) (0xe0 + (x))`, general
purpose data registers, and 0xe0 is `RK816_DATA_REG(0)`. The neighbours are the
same kind of thing — 0xe1–0xe4 saved remaining capacity, 0xe6–0xe9 saved full
charge capacity, 0xec reboot counter, 0xee software flags, 0xef halt counter.
They survive across reboot and across power-off only because the PMIC is
powered from the pack, not because any hardware maintains them.

The real chain in the vendor driver is two layers:

**`rsoc`, the honest number.** `rk816_bat_get_coulomb_cap()` at `:434` reads
the 32-bit counter and divides by 2390; `rk816_bat_get_rsoc()` at `:451` is
`(remain_cap + fcc/200) * 100 / fcc`. That is a pure integral of current — it
cannot jump, and it drifts.

**`dsoc`, the number reported to userspace.** `rk816_battery_get_property()`
at `:1154` returns `di->dsoc`, never `rsoc`. `dsoc` is produced by about 1500
lines of smoothing that pull it towards `rsoc` at a controlled rate: five work
modes (`MODE_ZERO`, `MODE_FINISH`, `MODE_SMOOTH_CHRG`, `MODE_SMOOTH_DISCHRG`,
`MODE_SMOOTH`), a `zero_linek` slope so the last few percent land on the
cut-off voltage instead of falling off a cliff (`:2409`), a charge-side slope
(`:2373`), age-based FCC relearning (`:2832`), a sleep-discharge estimate
(`:3248`), a reboot counter that resyncs `dsoc` to `rsoc` after 80 reboots in
three minutes (`:3375`), and a per-tick remainder saved to 0xf0/0xf1 so the
slope survives a reboot (`:3572`).

Boot-time seeding, `rk816_bat_init_rsoc()` at `:2067`, has three cases:

- `GGSTS.BAT_CON` set → first power-on: OCV register → `ocv_table` → both
  `rsoc` and `dsoc` (`:1900`).
- otherwise trust the saved 0xe0 byte, except that if the PMIC was off 30
  minutes or more (`is_rk816_bat_ocv_valid`, `:1030`) the OCV is re-read and
  wins when it differs by `max_soc_offset` (60 on this board), and if the
  coulomb counter moved more than 10 % of FCC while unattended the system is
  assumed to have halted and the counter is trusted instead (`:1886`).

## 3. The minimum honest driver

`drivers/power/supply/rk816_charger.c` in patch `0011`, about 1000 lines
against the vendor's 5000. Shape borrowed from `rk817_charger.c`: a delayed
work every 8 s, plug-in and plug-out interrupts from the MFD cell, and
`power_supply_get_battery_info()` for the pack description.

Three supplies registered as **`battery`**, **`usb`** and **`ac`** — not
`rk817-charger`'s `rk817-battery` / `rk817-charger`, because
`/sys/class/power_supply/{battery,usb,ac}` is the contract `src/status.c`
reads. That fills all six files:

| file | source |
| --- | --- |
| `battery/capacity` | coulomb counter ÷ FCC |
| `battery/status` | 0xa0 bits 6:4, forced to Discharging when nothing is online |
| `battery/current_now` | 0xbc/0xbd, positive when charging, as the vendor reports it |
| `battery/voltage_now` | 0xc4/0xc5 with the factory k/b and the 1.1 divider |
| `usb/online` | extcon SDP or CDP, else `PLUG_IN_STS` |
| `ac/online` | extcon DCP, else always 0 |

**Why the percentage does not jump under load.** It is never derived from the
loaded terminal voltage. A voltage lookup on this pack moves by ten points or
more when the GPU load steps, because `bat_res` is 100 mΩ and the load swing is
several hundred milliamps. The coulomb counter is an integral: it moves at
most by the charge that actually flowed since the last poll. The only two
moments the counter is written are

1. once at probe, seeding it from the relaxed OCV via the DT `ocv-capacity-table`
   — but only on a first connect (`GGSTS.BAT_CON`), after ≥ 30 minutes powered
   off, or when the saved byte is out of range; otherwise the saved byte is
   trusted and there is no discontinuity across a reboot at all;
2. at `CHARGE_FINISH`, where the counter is re-anchored to FCC. This is the one
   place the true charge is known and the one correction that can step the
   number, always upward and always at 100 %.

Everything else is `clamp(counter, 0, fcc) * 100 / fcc`.

### What is lost against the vendor driver

Deliberately dropped, in rough order of how much it matters:

- **The zero algorithm.** The vendor bends the last few percent so 0 % arrives
  at the 3.45 V cut-off under the real load. Here the counter can reach 0 %
  while the pack still has usable charge, or the pack can hit the under-voltage
  shutdown while the driver still says 3 %. The appliance should treat the
  voltage, not the percentage, as the shutdown trigger.
- **FCC relearning / ageing.** `fcc` here is the saved value, sanity-clamped,
  and is only ever reset to design capacity. The vendor tracks a full
  discharge-to-charge cycle and rewrites FCC (`:2832`), and reads the
  hardware's `FCC_LOCK` latch (`:1409`). An aged pack will read optimistically.
- **Sleep accounting.** `rk816_bat_sleep_dischrg()` (`:3248`) estimates what
  was consumed in suspend from the relax voltage and the RTC delta. Here the
  counter keeps counting in suspend but nothing corrects it, and the driver
  simply resumes polling.
- **ADC offset self-calibration.** `rk816_bat_adc_calib()` (`:807`) recalibrates
  the current offset while sitting at charge-finish. Without it a small
  systematic current error accumulates into the counter between full charges.
- **Temperature.** No thermistor is fitted on this board (the vendor node has
  no `ntc_table`), so `POWER_SUPPLY_PROP_TEMP` is not offered and the OCV table
  is looked up at a fixed 20 °C, the same assumption `rk817_charger.c` makes.
- **OTG / boost management, DC jack, LEDs, low-power input throttling,
  `virtual_power` test mode, the sysfs debug interface, the `fb_notifier`
  screen-blank hook, the halt/reboot counters.** None of these are needed here
  and several are actively unwanted upstream.
- **`max_soc_offset` forcing and the `dsoc`/`rsoc` split itself.** There is one
  number, and it is the counter's.

Kept because dropping them would be dishonest or dangerous: the factory voltage
calibration, the sense-resistor scaling, the charge voltage/current/termination
programming from the pack description, both input limiters, the relax
thresholds, digital termination, the disabled CCCV safety timer, and the
`PLUG_IN_STS` override of extcon.

### One MFD bug found on the way

`rk816_is_volatile_reg()` in `drivers/mfd/rk8xx-i2c.c` had
`case RK816_GASCNT_REG(0) ... RK816_BAT_VOL_REGL:`. `RK816_GASCNT_REG(x)` is
`0xbb - x`, so `(0)` is the **low** byte: 0xb8–0xba, the three high bytes of the
coulomb counter, were cached, as were the relax registers, the ADC offset
calibration and the power-off minute counter. The RK816 regmap is
`REGCACHE_MAPLE`, so those would have been frozen at their first post-boot
value for the life of the boot — a silently stuck gauge. Patch `0010` widens the
range to 0xb8–0xdf.

## 4. Device tree

New `charger` node under the PMIC, mirroring the RK817 binding
(patch `0012` adds it to `Documentation/devicetree/bindings/mfd/rockchip,rk816.yaml`,
which had `additionalProperties: false` and so rejected it):

- `monitored-battery` — required, phandle to a `simple-battery`
- `rockchip,resistor-sense-micro-ohms` — required, 10000 or 20000
- `rockchip,sleep-enter-current-microamp` — required
- `rockchip,sleep-filter-current-microamp` — required
- `input-current-limit-microamp` — optional, the board limit for a DCP

plus an optional `extcon` phandle on the PMIC node itself, pointing at the SoC
USB PHY, without which every charger is a 450 mA port and `ac/online` is
permanently 0.

The `simple-battery` node needs `charge-full-design-microamp-hours`,
`charge-term-current-microamp`, `constant-charge-current-max-microamp`,
`constant-charge-voltage-max-microvolt`, `voltage-max-design-microvolt`,
`voltage-min-design-microvolt`, and an `ocv-capacity-table-0`.

### What the board DTS must add

`arch/arm/boot/dts/rockchip/rk3126-taq102.dts` currently has **no PMIC node at
all**, so this is more than a charger node. Patch `0013` appends, as one hunk:

- `&i2c2 { status = "okay"; }` with `pmic@1a`, `compatible = "rockchip,rk816"`,
  `interrupt-parent = <&gpio0>`, `interrupts = <RK_PA2 IRQ_TYPE_LEVEL_LOW>`,
  `#clock-cells`, `clock-output-names`, `gpio-controller`, `extcon = <&usb2phy>`,
  `wakeup-source`, and the `charger` child.
  **The PMIC is on i2c2, not i2c0** — i2c0 on this board is the GSL3673 touch
  controller at 0x40. Verified by decompiling the produced dtb: the node comes
  out `interrupts = <0x02 0x08>`, byte for byte the vendor dtb's.
- a `pmic_int_l` pinctrl group, `<0 RK_PA2 RK_FUNC_GPIO &pcfg_pull_default>`,
  from the vendor's `pmic-int-l` (`<0x00 0x02 0x00 0x50>`, and `0x50` there is
  `pcfg_pull_default`).
- `&usb2phy` and `&usb2phy_otg` enabled. They are `disabled` in `rk3128.dtsi`,
  which means the ACM gadget this board uses as its only console cannot probe
  today either; the PHY is also what does the BC1.2 detection.
- the `battery` node, straight from the vendor board file:

| vendor property | value | DT property |
| --- | --- | --- |
| `design_capacity = <0x10af>` | 4271 mAh | `charge-full-design-microamp-hours = <4271000>` |
| `max_chrg_current = <0x578>` | 1400 mA | `constant-charge-current-max-microamp = <1400000>` |
| `max_chrg_voltage = <0x1068>` | 4200 mV | `constant-charge-voltage-max-microvolt = <4200000>` |
| `power_off_thresd = <0xd7a>` | 3450 mV | `voltage-min-design-microvolt = <3450000>` |
| `bat_res = <0x64>` | 100 mΩ | `factory-internal-resistance-micro-ohms = <100000>` |
| `max_input_current = <0x640>` | 1600 mA | `input-current-limit-microamp = <1600000>` |
| `sleep_enter_current = <0x12c>` | 300 mA | `rockchip,sleep-enter-current-microamp = <300000>` |
| `sleep_filter_current = <0x64>` | 100 mA | `rockchip,sleep-filter-current-microamp = <100000>` |
| (absent, so default 20) | 20 mΩ | `rockchip,resistor-sense-micro-ohms = <20000>` |
| `ocv_table` (21 entries, 0→100 % in 5 % steps) | 3450…4200 mV | `ocv-capacity-table-0`, reversed: the core wants highest voltage first |

Termination current is not in the vendor DTS; it is computed by
`rk816_bat_finish_ma()` (`:2217`) from FCC, and 4271 mAh ≥ 4000 gives 200 mA,
hence `charge-term-current-microamp = <200000>`.

`design_qmax = <0x125a>` (4698 mAh) has no DT equivalent and is not needed: it
only bounds the vendor's FCC relearning, which this driver does not do.

Deliberately **not** added: the `regulators` subtree and
`rockchip,system-power-controller`. Nothing above needs either, the regulator
node names differ from the vendor's (`dcdc1`…`ldo6`, not `DCDC_REG1`…), and a
mistranscribed rail constraint is how this board stops booting. That is a
separate change with its own risk.

## Applying

From the root of `/work/linux-mainline`, in order; all four are `git apply`
clean against 7.3.0-rc2 as of this writing:

```
git apply 0010-mfd-rk8xx-add-the-rk816-charger-cell.patch
git apply 0011-power-supply-rk816-battery-and-charger.patch
git apply 0012-dt-bindings-mfd-rockchip-rk816-charger-node.patch
git apply 0013-arm-dts-rk3126-taq102-rk816-battery-and-charger.patch
```

`0013` is one hunk appended at the end of the board file, and it re-includes
`interrupt-controller/irq.h` and `pinctrl/rockchip.h` inside that block rather
than at the top, so it does not collide with the touchscreen patch that edits
the top of the same file. Order between the two does not matter.

Config: `CONFIG_CHARGER_RK816=y` (or `=m`), which needs `CONFIG_MFD_RK8XX`;
`CONFIG_EXTCON` and `CONFIG_PHY_ROCKCHIP_INNO_USB2` for the USB/AC split.

## Verification status

- `drivers/power/supply/rk816_charger.c` compiles clean under the Buildroot
  GCC 14.3 arm cross compiler with the mainline headers and `-Wall`, no
  warnings. Command used (no `make` was run in the shared tree):

  ```
  arm-buildroot-linux-gnueabihf-gcc -nostdinc -fms-extensions \
    -I$K/arch/arm/include -I$K/arch/arm/include/generated -I$K/include \
    -I$K/arch/arm/include/uapi -I$K/arch/arm/include/generated/uapi \
    -I$K/include/uapi -I$K/include/generated/uapi \
    -include $K/include/linux/compiler-version.h \
    -include $K/include/linux/kconfig.h \
    -include $K/include/linux/compiler_types.h \
    -D__KERNEL__ -DMODULE -D__LINUX_ARM_ARCH__=7 \
    -std=gnu11 -Wall -march=armv7-a -msoft-float -fsyntax-only rk816_charger.c
  ```

- The board DTS preprocesses and compiles with `scripts/dtc/dtc` with no errors,
  and the decompiled result was checked against the vendor dtb.
- **Nothing has been run on the tablet.** No register was read from real
  hardware; every value here comes from the vendor source or the vendor dtb.
