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

## Still open

The usb2phy's charger detection reporting nothing at all is a separate bug and
is now its own backlog item. With it working, a real wall charger would
enumerate as DCP and this board would take its 1.6 A; a laptop port would
enumerate as SDP and be held to 450 mA, which is correct and would discharge
this appliance at full brightness. The declared limit is what makes the
appliance usable meanwhile.
