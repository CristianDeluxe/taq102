# RK816 input limit after the power cut

I supplied the initial measurements; they were not repeated. The
running mainline tablet had survived the mains outage on battery. After power
returned, USB was online, all extcon states were zero, USB_CTRL (0xa1) was
0x40, and the battery was discharging at about 223 mA. Writing 1500000 to
`usb/input_current_limit` succeeded but left the register unchanged for 30 s;
the property nevertheless read 1500000.

## Immediate recovery, 2026-09-13

One authorised write over SSH to root@192.168.1.71:

```sh
i2cset -f -y 2 0x1a 0xa1 0x45
```

The write kept VLIM at 4.4 V and selected index 5, 1500 mA. Subsequent
`battery/current_now` samples, in microamps:

| UTC | Seconds after write | Current |
| --- | ---: | ---: |
| 17:24:49 | 0 | -234936 |
| 17:24:54 | 5 | -234936 |
| 17:24:59 | 10 | 635532 |
| 17:25:04 | 15 | 634026 |
| 17:25:09 | 20 | 634026 |
| 17:25:14 | 25 | 640050 |
| 17:25:19 | 30 | 640050 |

The final register read was 0x45 and sysfs read 1500000. The tablet was left
charging. No display, desk, boot, BCB, firmware or kernel changes were made on
the device. This proves that this supply and hardware could charge under the
current load once the 1500 mA setting reached the chip. It does not certify the
supply under every load or guarantee charging through another power outage.

## Source diagnosis

The existing VM source at `/work/linux-mainline/drivers/power/supply/rk816_charger.c`
matched the original repository patch byte for byte.

The missing-notifier hypothesis is incorrect for the explicit write:
`rk816_usb_desc.set_property` selects `rk816_usb_set_prop`, which immediately
calls `rk816_bat_set_input_current` under the charger mutex. Only subsequent
cable classification consults the saved override in `rk816_bat_update_cables`.

The getter is also the expected one: `rk816_usb_desc.get_property` selects
`rk816_usb_get_prop`, whose INPUT_CURRENT_LIMIT case calls
`rk816_bat_get_input_current`. It is not returning the saved override directly.
However, its `regmap_read(USB_CTRL)` can return a cached register value.

The RK816 MFD uses REGCACHE_MAPLE. USB_CTRL was missing from
`rk816_is_volatile_reg`, although VB_MON was already volatile. In the VM's
regmap implementation, `_regmap_read` returns a cache hit first, and
`_regmap_update_bits` skips the write when the computed value equals that cached
value. A cached 0x45 with actual hardware 0x40 therefore explains BOTH the
1500000 property and the successful no-op write. The experiment establishes
recovery when that skipped write reaches hardware. The exact event that first
changed the hardware behind the cache was not captured; do not claim to have
proved the PMIC's reset timing or mechanism.

`plugged_in` already comes from `PLUG_IN_STS` in PMIC VB_MON (0x21), refreshed
by the eight-second monitor and the PMIC plug interrupts. It is independent of
extcon. `rk816_bat_read_props` maps present but unclassified VBUS to USB online,
which explains the supplied online=1. All-zero extcon states describe missing
classification, not necessarily missing VBUS.

The cache/skip behavior was checked against both the actual VM kernel source
and the current [upstream regmap implementation](https://github.com/torvalds/linux/blob/master/drivers/base/regmap/regmap.c).

## Patch changes

- Classify USB_CTRL as volatile in the MFD. Both property reads and masked
  updates now use the live register, so the getter reports the programmed
  hardware ceiling rather than cached intent. This ceiling is not a promise
  of actual current: voltage limiting can still reduce the draw.
- Use `regmap_write_bits` for the input-current helper. Each accepted request
  forces a masked hardware write, including repeated requests, preserving the
  live VLIM and other upper bits. Existing rounding and I/O errors remain.
- Read the PMIC presence bit on the sysfs write itself. Accept an override with
  VBUS present even when extcon is mute; return ENODEV when absent, or the I/O
  error if presence cannot be read. Save an override only after a successful
  register write.
- Handle PLUG_OUT explicitly: clear the override and restore 450 mA under the
  mutex, then schedule the usual monitor. This also expires the override when
  VBUS returns before the deferred monitor samples the disconnect. The
  existing polling clear remains as a fallback.

This does not repair BC1.2 classification and does not add an automatic
high-current fallback for unknown ports.

## Automatic policy recommendation, not implemented

The appliance's existing `rootfs-overlay/init` writes 1600000 once at boot
(rounded down to 1500000). A mains interruption while the tablet stays running
never reruns that code.

For the explicitly provisioned, known supply, add a small supervised appliance
service: apply the configured limit at boot when online and after a stable
VBUS return, watching power-supply events with a periodic poll as fallback.
Use USB OR AC online so DCP classification is not mistaken for disconnect.
Check the live limit while online too, so a brief drop missed by userspace but
caught by the kernel's unplug interrupt is recovered. Write only when the
limit differs; verify the rounded readback, log failures, and retry with a
bounded delay. Make persistent plugged-in discharge visible after a settling
interval. Keep this policy explicitly tied to the appliance's known supply;
it must not silently authorise 1500 mA from an arbitrary laptop USB port.

No service or policy was implemented or installed in this task.

## Verification and limits

- Applied the complete updated patch with `git apply --check`, then applied it
  to temporary copies of the VM kernel's base files. The VM tree was not edited.
- `python3 tools/test-rk816-input-current.py` passed. It extracts the actual
  patched callbacks and compiles a userspace harness with `-Wall -Wextra
  -Werror`, using a fake regmap. It reproduces the old cached failure and checks
  live reporting, immediate/repeated writes, preservation of live VLIM, current
  table boundaries, errors, mutex release and unplug expiry after quick return.
  It also checks the patch's volatile declaration. It is not a kernel test.
- No kernel build: source inspection, patch application and the userspace
  experiment suffice to establish the fault and review this correction.
  Compilation of the complete driver/MFD, IRQ concurrency and the corrected
  sysfs behavior on a running kernel remain unverified. Qualifying those needs
  a later kernel build and authorised boot, then cable/power-cycle tests.
- The desk host suite was not run: it does not cover the kernel. Its supplied
  baseline is 39 passed, zero failed unsandboxed; the unrelated intentionally
  failing geometry audit was preserved.

The 2026-09-12 blocked limiter item remains separate. That drain occurred with
0xa1 already 0x45. Today's write changed 0x40 to 0x45, so it does not establish
whether rewriting an unchanged 0x45 releases the earlier voltage limiter
symptom. Do not infer a periodic rewrite or a VLIM toggle policy from this
experiment.
