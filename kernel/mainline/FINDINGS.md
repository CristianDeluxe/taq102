# Mainline on the TAQ-102: what was found, and how

Linux 7.3.0-rc2 runs on this tablet with battery, storage, Wi-Fi, USB and the
panel. Getting there turned up **two bugs in mainline itself**, neither
board-specific, and cost eleven flashed images. This is the record of what was
actually established, so none of it has to be re-derived.

The narrative of the search is in `ISOLATING-THE-DISPLAY-HANG.md`; the images,
their variables and their results are there too. This file is the conclusions.

## What works, measured on the device

| subsystem | evidence |
| --- | --- |
| boot | `first-mainline-boot-console.log` -- arch timer at 24 MHz, both memory banks |
| battery | RK816 driver from the port: `capacity=100`, `status=Full`, `voltage_now=4153600`, `current_now=-3012`, `usb/online=1` |
| storage | eMMC at `mmcblk1`, `/data` mounted ext4 after journal recovery |
| Wi-Fi | in-tree `rtw88_8723cs`: authenticated, associated, DHCP lease, mdnsd announcing |
| USB | ACM gadget enumerates; the console this whole hunt depended on |
| **panel** | kernel console visible on the tablet's screen at the panel's own 1024x600, connector `connected`, physical size 125x223 mm |

## Bug 1 — a genpd deadlock blocks every driver on the video domain

**Symptom.** Any kernel with the Rockchip DRM built in booted to a silent white
screen: no console, no network, nothing. Six attempts produced not one byte of
diagnostic output.

**What it actually is.** Not a crash. `insmod` blocks in D state while the rest
of the system keeps running -- the beacon kept flashing and the shell stayed
alive throughout. Built in, that same wait happens in an initcall, so the boot
never reaches userspace and the machine *looks* dead.

```
task:insmod          state:D
 __mutex_lock.constprop.0 from genpd_add_device+0xd4/0x264
 genpd_add_device from __genpd_dev_pm_attach+0xa0/0x284
 __genpd_dev_pm_attach from genpd_dev_pm_attach+0x58/0x60
 genpd_dev_pm_attach from dev_pm_domain_attach+0x24/0x44
 dev_pm_domain_attach from platform_probe+0x40/0x90
```

`wchan` is `genpd_add_device`; the mutex is `genpd->mlock`, one domain's lock,
not the global list lock. Only one task is in D state, so the holder is not
itself blocked on I/O. Every boot logs, before any of this:

```
rockchip-pm-domain ...: sync_state() pending due to 1010e000.vop
rockchip-pm-domain ...: sync_state() pending due to lvds
rockchip-pm-domain ...: sync_state() pending due to 20038000.phy
```

The provider waits for those consumers to probe before running sync_state, and
`genpd_provider_sync_state()` takes `genpd_lock()` in its SIMPLE case.

**Proved not to be our code**: mainline's own PHY driver, built from a clean
tree and verified by content to carry none of our patches, blocks identically.

**Workaround in use**: `fw_devlink=off` on the command line. The four
sync_state lines vanish and the module loads in 70 ms.

**The proper fix, not yet tested**: `GENPD_FLAG_NO_SYNC_STATE` on the Rockchip
domains, which today set only `GENPD_FLAG_PM_CLK | GENPD_FLAG_NO_STAY_ON`.

Evidence: `genpd-deadlock-stack.txt`, `drm-first-bind-fw_devlink-off.log`.

## Bug 2 — the Rockchip LVDS driver hides its own panel

**Symptom.** With the deadlock gone the display came up, but the connector
offered generic modes -- 1024x768, 800x600 -- and never the panel's 1024x600.
The panel was bound, the DT graph correct at both ends, no errors logged.

**Cause**, four lines apart in `rockchip_lvds_bind()`: it wraps the panel with
`drm_panel_bridge_add_typed()`, sets `lvds->panel = NULL` because the bridge
owns it now, then overwrites *that bridge's* funcs with its own -- whose
`get_modes()` reads `lvds->panel`. Instrumentation settled it in one boot:

```
rockchip-lvds lvds: panel=4fcc1c99 bridge=00000000 endpoint_id=0 children=1
rockchip-lvds lvds: get_modes panel=00000000 returned 0
```

Found at bind, NULL when the modes are asked for.

**Fix**: `0010-drm-rockchip-lvds-do-not-hijack-the-panel-bridge.patch`. Only
claim the bridge's ops for a bridge found in the DT; a panel bridge already
answers `get_modes` correctly. After it the connector offers exactly 1024x600
and fbcon resizes 128x48 -> 128x37.

Any `rockchip,*-lvds` driving a panel rather than a bridge hits this.

Evidence: `lvds-panel-bridge-bug.txt`, `panel-working-console.log`.

## Hypotheses that were wrong, and why they are worth recording

Each of these looked strong, and each cost at least one flashed image. They are
listed so nobody spends the evening on them twice.

- **Kernel too large, overwriting the ramdisk.** `multi_v7_defconfig` builds 163
  ARM platforms in; trimming to Rockchip alone took the decompressed Image from
  33.7 MB to 25.7 MB. Still silent -- and the measured addresses say it never
  overlapped: the FDT sits at 0x64600000, the initrd at 0x64bf0000, our Image
  ends at 0x619C6F1C.
- **The machine compatible.** `mach-rockchip/rockchip.c` lists neither rk3126
  nor rk3128, but `devtree.c` falls back to GENERIC_DT and `time.c` then calls
  `of_clk_init()` and `timer_probe()` itself. The in-tree rk3128-evb has the
  same property.
- **`CLK_RK312X` missing.** An earlier note in this repository said so; that was
  a check for `CLK_RK3128`, which is not a symbol. The driver was always built.
- **The EFI `MZ` header.** `efi-header.S` emits two `eor` instructions that
  cancel out; the legacy entry still works. The `EEEE` at 0x34 is zImage
  extension metadata and stays either way.
- **The missing `HCLK_VIO_H2P` gate.** A good hypothesis, well founded: the
  vendor enables four clocks before touching a PHY register where mainline
  names two, and that gate has neither `CLK_IS_CRITICAL` nor
  `CLK_IGNORE_UNUSED`, so it comes up off. The hardware says it is not the
  cause. The clock patch is kept anyway; it is correct on its own terms.
- **The power-domain description.** Mainline's rk3128 table matches the
  vendor's bit for bit (VIO = pwr 3, status 3, req 2).
- **Our own PHY patches.** Suspected for a day, exonerated twice: the pristine
  module blocks identically, and the whole path works once the deadlock is out
  of the way.

## Two things about method that cost more than they should have

**A diagnostic channel that depends on the thing being diagnosed is not a
channel.** The USB gadget needed userspace, which was never reached. ramoops
lives in RAM, and the only recovery from a failed boot holds power until the
PMIC drops the rails -- so the region came back full of freshly powered DRAM
every time. The backlight beacon runs near the end of `/init`. What finally
worked was the framebuffer the *bootloader* leaves scanning out
(`uboot_logo=0x02000000@0x9dc00000`): it is there before the kernel runs and
needs nothing from it. That white screen everyone had been staring at *was*
the channel.

**One variable per image, or the result means nothing.** v61 changed the DRM
stack and the reboot-mode node together and its silence proved neither. And
every variant cost a physical button dance for one bit of information until the
display stack was built as modules -- after which one boot bought every
remaining experiment, because the pieces could be inserted by hand with a
console to watch.

## Still open

- Replace `fw_devlink=off` with `GENPD_FLAG_NO_SYNC_STATE` and A/B it.
- Send both patches upstream, with the stack trace.
- lima needs `gpu-sched` in the initramfs (`lima: Unknown symbol
  drm_sched_init`) before glcube has a GPU. Packaging, not a defect.
- `modetest` cannot create a dumb buffer (-EINVAL); understand that before
  blaming Mesa for anything.
- Touch: the GSL3673 answers with chip ID 0x50910000 and then fails a register
  write with -6, so the bus is fine and the chip is alive. Untouched since.
