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

**Fix**: patch 0009 (drm/rockchip: lvds: do not take over a panel bridge's funcs). Only
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

## The cube runs on mainline (2026-09-09)

v66: the tablet boots Linux 7.3.0-rc2 and, 14 seconds after power-on, `glcube`
is drawing through Mesa 26.0.1's lima driver on the Mali-400 MP2 at 52.8 FPS,
1024x600 on the panel, `glGetError 0x0`. Evidence:
`docs/evidence/2026-09-09-cube/`.

It took three images after the panel worked, and the first two are the
lesson from the method section applied backwards:

- **v62, v63 -- the display stack built in, hangs before userspace.** v62 was
  everything =y; v63 added `GENPD_FLAG_NO_SYNC_STATE` to the Rockchip power
  domains, the "narrow fix" this file had proposed. Both: white screen, no
  gadget, no console. So `fw_devlink=off` was never a workaround for a bug that
  flag fixes on its own; whatever the full mechanism is, a built-in PHY still
  blocks at boot with `NO_SYNC_STATE` set. The narrow fix is not one line, and
  the workaround stays.
- **v64 -- variant M's kernel byte for byte, M's ramdisk plus `gpu-sched.ko`.**
  Boots, panel comes up when the PHY is loaded, lima loads -- and finds no
  device: `rk3128.dtsi` declares `gpu@10090000` with `status = "disabled"` and
  the board DTS never enabled it.
- **v65 -- `&gpu { status = "okay"; };`.** lima probes: gp and two pp at
  version 1.1, 64 K L2, bus and core at 148.5 MHz, `renderD128`. `glcube`
  run by hand: `GL_RENDERER: Mali400`, 52.8 FPS.
- **v66 -- inittab runs `taq102-cube app`** (PHY, then `drm_shmem_helper`,
  `gpu-sched`, `lima`, then `taq102-app`), the backlight beacon is off. The
  cube comes up by itself.

The kernel of v64-v66 is variant M unchanged (`zImage-7.3.0-rc2-variant-M` in
the archive): VOP, LVDS encoder and panel built in, PHY and Lima as modules,
`fw_devlink=off`, patch 0009. lima's `mali` regulator is optional and absent
(the vendor hands it `vdd_logic`; mainline has no RK816 regulators described
yet), so the GPU runs at whatever the bootloader left ACLK_GPU at, 148.5 MHz.
The cube does not need more.

### And then it stopped: devfreq with no regulator (v66 -> v67)

Seventy-three seconds into the first unattended run the cube froze. `dmesg`:
`pp0 job timeout`, `pp0 bus stop timeout`, `ppmmu0 command 2 timeout`, every
ten seconds, 157 times, and lima's reset never brought the core back.
`clk_summary` said `aclk_gpu 480000000` where the probe had printed
`bus rate = 148500000`, and `/sys/class/devfreq/10090000.gpu/trans_stat`
explained it: `simple_ondemand` over the `rk3128.dtsi` OPP table, 150
transitions between 200 and 480 MHz in the first minute, with no `mali-supply`
to move the voltage along with the clock. 480 MHz at whatever `vdd_logic` the
bootloader left is where the Mali gave up.

v67 deletes `operating-points-v2` from `&gpu` and nothing else. lima keeps
the bootloader's 148.5 MHz, there is no devfreq device, and the cube draws
52.8 FPS exactly as before -- the panel's 56 Hz is the limit, not the clock.
Evidence: `docs/evidence/2026-09-09-cube/v66-gpu-hang-devfreq-480mhz.txt` and
`v67-console-no-devfreq.log`. The OPP table comes back with the regulators.

### Touch: the reset write is NAKed and works anyway (v68)

`silead_ts 2-0040: Silead chip ID: 0x50910000` then `Registers clear error -6`
on every boot. -6 is ENXIO, the rk3x-i2c driver's word for a NAK. From
userspace with the driver unbound: reads of every register answer, writes to
0xe4, 0xbc, 0x80, 0x00 and 0xf0 are acknowledged, and only `0xe0 = 0x88`, the
reset command, is NAKed -- yet 0xe0 reads 0x00 after an acknowledged
`0xe0 = 0x00` and 0x80 right after the NAKed 0x88. The controller halts on
the command before it acknowledges the byte. The vendor driver, like every
driver descended from Silead's reference code, never checks the return of
that write. Patch 0011 accepts -ENXIO on that one write, in
`silead_ts_init()` and `silead_ts_reset()`, for chips flagged with the
quirk; the firmware has no 0xe0 entries.
v68: probe completes, `input0 = silead_ts`, firmware in 9 s at 100 kHz.
Whether events arrive is my test; evidence so far in
`docs/evidence/2026-09-09-cube/v68-console-silead-probes.log`.

## The series, reviewed (2026-09-09)

A strict read of the eleven patches against the tree, with each finding
checked before it was acted on:

- **0011 conflicted with 0007.** It had been cut as the whole `silead.c` diff
  and carried 0007's id-table hunks, so it could not apply after 0007. Fixed.
- **No messages, no Signed-off-by** on most of the series. The series is now
  `git format-patch` output from a branch built on `28924df2a`: twelve
  commits, each with a message and a Signed-off-by, `git am`-clean as a set.
- **`rk312x_lvds_probe()` leaked the PHY**: `phy_init()` succeeded and the
  `phy_set_mode()` and `phy_power_on()` error paths returned without
  `phy_exit()`. Fixed in 0003. `px30_lvds_probe()` upstream has the same
  shape and is left alone; that is a separate patch if anyone wants it.
- **`rockchip,rk3126-lvds` had no binding.** 0002 adds it to
  `rockchip,lvds.yaml`, in the PX30 clause (phys required, no reg, no
  clocks), which is the shape the node has.
- **The NAK tolerance was unscoped.** 0010 accepted -ENXIO on the reset
  write for every Silead chip; it now applies only to entries flagged
  `SILEAD_QUIRK_RESET_NAK`, which is the gsl3673 id and compatible.
- **RK816 message claimed nothing had run on hardware**, written before it
  had. Replaced with the measured values. Its `fcc_uah / 100` could reach
  zero for a design capacity under 100 uAh; the probe now requires the
  vendor's 500 mAh floor.
- **Stale numbers** in the README and the config fragment, from before the
  first reconciliation. Updated to the new numbering; the README carries the
  mapping.
- **Not changed, on purpose:** the monitor work in the RK816 driver reads
  `soc`, `charge_status` and the online flags for its did-it-change
  comparison after dropping the lock. Word-sized reads, worst case one
  spurious or missed `power_supply_changed()`. Noted for the maintainer.
- **Checked and found correct:** 0006's claim that the `apb` reset was never
  used elsewhere in the driver (it is not); 0005's evidence is stated with
  the right weakness.

v69 carries the reviewed tree and runs: panel, cube at 52.8 FPS, battery
values, `silead_ts` probing. `dt_binding_check` has not run -- the VM has no
`dtschema`.

## Still open

- Replace `fw_devlink=off`. `GENPD_FLAG_NO_SYNC_STATE` alone does not do it
  (v63); the built-in PHY still blocks at boot. Needs the trace from a built-in
  boot, which means a diagnostic channel that survives the hang -- the
  bootloader framebuffer with an early printk of the blocked task.
- Send upstream: 0009 (the LVDS panel-bridge fix) and 0010 (the silead NAK
  quirk) are `git am`-shaped and scoped; 0001-0004 want a `dt_binding_check`
  run first. The genpd deadlock goes as a report with the stack trace.
- Package the modules. The v66 ramdisk is hand-assembled: `gpu-sched.ko`,
  `lima.ko`, `drm_shmem_helper.ko` and `phy-rockchip-inno-dsidphy.ko` copied
  from the kernel build into `rootfs-v55` plus `taq102-cube`. The Buildroot
  mainline image should get them from the kernel's `modules_install`.
- Describe the RK816 regulators in the board DTS, give lima `vdd_logic` as
  `mali-supply`, and only then restore the GPU OPP table: devfreq without a
  regulator hung the Mali at 480 MHz (v66). 148.5 MHz draws the cube at the
  panel rate, so this is not urgent.
- `modetest` cannot create a dumb buffer (-EINVAL). Moot for glcube, which
  allocates through GBM and lima; still worth understanding.
- Touch: probes since v68 (patch 0010). Events not yet seen; the interrupt
  count was zero before anyone had touched the glass.
  GSL3673 answers with chip ID 0x50910000 and then fails a register write with
  -6. Untouched since.
