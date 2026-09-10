# Evidence, 2026-09-10: the tablet discharged while reporting Charging

I asked why the battery kept falling with the cable in. The battery
node said `status=Charging` and `current_now=-301200`: 301 mA leaving the cell.
`status` is not the number to read on this board, and `README.md` has said so
since 2026-09-04.

## What it costs to run

`current_now` in mA, positive charging, measured over 12 s each with the input
limit at its old 450 mA:

| state | current |
| --- | --- |
| cube, brightness 255 | -259 |
| cube, brightness 128 | -22 |
| cube, brightness 40 | +75 |
| no cube, brightness 40 | +164 |
| no cube, brightness 255 | -207 |

The backlight is the load, not the GPU: it swings about 300 mA on its own,
where the cube costs 50 to 90.

## The input limit was the whole story

`RK816_USB_CTRL_REG` (0xa1) read 0x40, so the low nibble was 0, index 0 of the
input table, **450 mA**. Raising it by hand with the panel at full brightness:

| index written | limit | current | battery |
| --- | --- | --- | --- |
| 0 | 450 mA | -245 mA | 3.990 V |
| 2 | 850 mA | +268 mA | 4.056 V |
| 5 | 1500 mA | +695 mA | 4.126 V |

So the port can feed the tablet perfectly well. Nothing was asking it to.

## Why the driver clamped it

`rk816_bat_update_cables()` sets the limit from the SoC USB PHY's BC1.2
detection, and takes 450 mA when it sees neither SDP, CDP nor DCP. On this
board that extcon reports **every cable as zero, including `USB` itself**, with
VBUS plainly present and the gadget console enumerated:

    /sys/class/extcon/extcon0  name=20008000.syscon:usb2phy@17c
      USB=0  USB-HOST=0  SDP=0  CDP=0  DCP=0  SLOW-CHARGER=0

That is not a port advertising SDP. That is no answer at all, and the board's
own DTS has declared `input-current-limit-microamp = <1600000>` since the
charger patch was written, where it was only ever read for the DCP case.

The fix, folded into patch 0008: when detection reports nothing, take the
board's declared limit; SDP keeps its 450 mA, and a board that declares nothing
still gets 450 mA because that is the property's default. v83 carries it.

## After the fix, from a clean boot with nothing poked

    0xa1 = 0x45   (index 5, 1500 mA)
    brightness 255, cube running, wlan0 up
    current_now = +624 mA, battery 4.144 V

## The review, and what the fix became

the reviewer was given the measurements and both source trees (`review-briefing.md`,
`review-second-opinion.md`) and rejected the first fix, correctly:

- The device tree property means **the maximum from a dedicated charging
  port**, which the binding says in as many words. Detection failing tells you
  nothing about the source, so reusing that number for "unknown" claims
  something the evidence does not support. What was measured is that *this*
  port delivers 1.5 A, not that any unclassified port will.
- The branch also fired where it should not have: on an ordinary disconnect
  every cable reads zero too, so unplugging programmed the high limit, and a
  negative `extcon_get_state()` collapses to false the same way.
- Two premises in the briefing were wrong. `EXTCON_USB` is published only for
  SDP in this PHY driver, so `USB=0` does **not** prove bvalid is low; and
  `dr_mode = "peripheral"` does not disable detection. So the all-zero snapshot
  does not by itself say where the detection breaks.

What the driver does now: an unclassified port is held to 450 mA like SDP, and
the usb supply gained a writable `POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT` with
hardware readback, so policy lives where the knowledge is. The override lasts
until the cable is pulled and is cleared on unplug. Two defects the same review
found in the existing helper are fixed with it: a request between 81 and 449 mA
used to round **up** to 450, and the register write's return value was
discarded.

The appliance's `init` raises the limit to the 1.6 A its own board declares,
which the driver rounds down to the 1500 mA the hardware offers. That keeps a
board whose detection is mute working without teaching the driver to guess.

Verified on v85 from a clean boot: `input_current_limit` reads 1500000, the
kernel log records the raise, and at brightness 255 with the cube running the
battery takes +584 mA.

## Still open

The usb2phy's charger detection reporting nothing at all is a separate bug and
is now its own backlog item. With it working, a real wall charger would
enumerate as DCP and this board would take its 1.6 A; a laptop port would
enumerate as SDP and be held to 450 mA, which is correct and would discharge
this appliance at full brightness. The declared limit is what makes the
appliance usable meanwhile.
