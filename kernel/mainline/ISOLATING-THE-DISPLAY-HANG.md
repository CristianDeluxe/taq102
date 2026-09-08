# Isolating the display hang

v59 boots to userspace every time: USB console, wifi with a DHCP lease, the
RK816 battery driver reporting real values, the beacon flashing. v61 is silent
-- no console, no beacon, a white panel.

**v61 changed two things at once**, which is a mistake in method: the Rockchip
DRM stack (VOP + LVDS + Lima + panel-lvds) *and* the `reboot-mode` DT node. So
the display cannot be blamed until they are separated.

Four images are built and waiting in
`/Volumes/Datos4TB2/denver-taq102/gate3-build/`. Each differs from v59 in one
respect. Flash with the watcher, which needs no timing on your part:

    tools/loader-watch.sh recovery /Volumes/Datos4TB2/denver-taq102/gate3-build/variant-<name>.img

| image | DRM | phy 0004 (analog power) | phy 0005 (reset pulse) | reboot-mode |
| --- | --- | --- | --- | --- |
| `variant-A-rebootmode-only` | off | on | on | **on** |
| `variant-B-drm-no-rebootmode` | **on** | on | on | off |
| `variant-C-drm-no-phy-patches` | **on** | off | off | off |
| `variant-D-drm-no-reset-patch` | **on** | on | off | off |

Read the result the same way every time: a USB console appearing at all means
the kernel reached userspace, and the beacon then flashes 1-4 to say where USB
got to. Silence with a white panel means it hung before that.

What each outcome establishes:

- **A boots** -- `reboot-mode` is innocent and the DRM stack is the suspect.
  It also means `reboot loader` works from the console, so no more button
  dances for the rest of this hunt. **Try A first for that reason alone.**
- **A hangs** -- the `syscon-reboot-mode` node is the fault, not the display,
  and the DRM work was never the problem.
- **B hangs, C boots** -- one of the two PHY patches is at fault; D then says
  which. This is the most likely branch: those two patches are ours, they are
  the least reviewed code in the series, and they have never run on hardware.
- **B and C both hang** -- the fault is in the VOP or LVDS driver patches
  (0001, 0002), not in the PHY. Note that the LVDS branch of the VOP patch
  does not set `pin_pol` where the RGB branch does; that is a real difference
  from both the RGB path and the vendor driver, and it is where I would look.
- **B boots** -- the DRM stack is fine, and the two changes interact. Unlikely,
  but it would be worth knowing.

The order that costs the fewest attempts is A, then B, then D, then C.


## Results, 2026-09-08 evening

| image | result | what it establishes |
| --- | --- | --- |
| **A** reboot-mode only, no DRM | **boots** | reboot-mode is innocent. And `reboot loader` now works from the console, so the button dance is no longer needed between attempts as long as a kernel boots. |
| **B** DRM, no reboot-mode | **hangs** | the fault is in the display stack. |
| **E** DRM + the missing h2p clock | **hangs** | the missing `HCLK_VIO_H2P` gate was a good hypothesis and is not the cause. |

Eliminated by reading, not by burning an attempt: the RK3128 power-domain table
in mainline matches the vendor's bit for bit (VIO = pwr 3, status 3, req 2), so
the domain description is not the problem.

### The change of approach that should have come three variants earlier

Each variant cost a physical button dance to recover, which buys one bit of
information per intervention. **F** ends that: the whole display stack is built
as modules -- rockchipdrm, the inno DSI PHY, panel-lvds, lima -- so the kernel
always boots to a console and the pieces are inserted by hand afterwards.

`/usr/sbin/taq102-drm-probe` in that image loads them one at a time and prints
what it is *about* to load before loading it, so if the machine dies the last
line names the piece that killed it. glcube's autostart is disabled there so it
cannot grab the panel mid-experiment.

    taq102-drm-probe phy    # the PHY alone; it never touches the VOP
    taq102-drm-probe drm    # PHY, panel, then rockchipdrm
    taq102-drm-probe        # everything, in dependency order

### G, for the one split F cannot make

`ROCKCHIP_LVDS` compiles *into* rockchipdrm.ko, so no amount of module loading
separates the VOP from the LVDS encoder. **G** does it in the device tree
instead: DRM built in, `&lvds` set to `disabled`.

- **G boots** -- the VOP is fine and the fault is in the LVDS encoder or the
  PHY it drives, which is where our own patches live.
- **G hangs** -- the VOP alone is enough to kill it, our LVDS work is not
  implicated, and the problem is mainline's VOP against a panel U-Boot left
  scanning out.

## Found it: a genpd deadlock in mainline, not in our patches

The module build paid for itself. With the display stack as modules the kernel
always reaches a console, and loading the PHY by hand reproduced the hang in
isolation -- without the VOP, without the LVDS encoder, without anything of
ours running.

**The system does not hang. `insmod` blocks.** The beacon kept flashing every
twelve seconds and the shell stayed alive throughout, which rules out the
"register access wedges the bus" theory that drove three earlier attempts.
It is a wait, not a crash -- and that is exactly why a built-in driver looked
like a dead machine: the same wait happens in an initcall, so the boot never
reaches userspace.

**And it is not our code.** `phy-pristine.ko`, mainline's own driver built from
a clean tree and verified by content to carry none of our three patches, blocks
in precisely the same place:

```
task:insmod          state:D
 __mutex_lock.constprop.0 from genpd_add_device+0xd4/0x264
 genpd_add_device from __genpd_dev_pm_attach+0xa0/0x284
 __genpd_dev_pm_attach from genpd_dev_pm_attach+0x58/0x60
 genpd_dev_pm_attach from dev_pm_domain_attach+0x24/0x44
 dev_pm_domain_attach from platform_probe+0x40/0x90
 ...
 do_one_initcall from do_init_module+0x50/0x224
```

`wchan` is `genpd_add_device`, and the mutex it waits on is `genpd->mlock` --
the lock of one power domain, not the global list lock. Only one task is in D
state, so whatever holds that mutex is not itself blocked on I/O.

The circumstantial evidence points at sync_state. Every boot logs, before any
of this:

```
rockchip-pm-domain 100a0000.syscon:power-controller: sync_state() pending due to 1010e000.vop
rockchip-pm-domain 100a0000.syscon:power-controller: sync_state() pending due to lvds
rockchip-pm-domain 100a0000.syscon:power-controller: sync_state() pending due to 20038000.phy
```

The domain is waiting for those three consumers to probe before it will run
sync_state; `genpd_provider_sync_state()` takes `genpd_lock()` in its SIMPLE
case. The Rockchip driver sets `GENPD_FLAG_PM_CLK | GENPD_FLAG_NO_STAY_ON` and
does **not** set `GENPD_FLAG_NO_SYNC_STATE`.

Evidence: `docs/evidence/2026-09-08-mainline/genpd-deadlock-stack.txt`.

### The cheap tests, in order

1. `fw_devlink=off` (or `permissive`) on the command line. If the block is the
   device-link/sync_state machinery, this sidesteps it and the PHY probes.
   One rebuild, no button dance -- reboot-mode is in the image now.
2. If that clears it, the honest fix is upstream-shaped rather than a boot
   argument, and the next question is whether the Rockchip domains should
   declare `GENPD_FLAG_NO_SYNC_STATE`.

## Confirmed, and the display comes up: `fw_devlink=off`

The hypothesis held. With `fw_devlink=off` on the command line the four
`sync_state() pending` lines disappear entirely (0 of them, against 4 on every
previous boot), the PHY module loads in 70 ms instead of blocking forever, and
the whole display stack falls into place behind it:

```
phy-ab: LOADED OK
rockchip-drm display-subsystem: bound 1010e000.vop
rockchip-drm display-subsystem: bound lvds
[drm] Initialized rockchip 1.0.0 for display-subsystem on minor 0
Console: switching to colour frame buffer device 128x48
rockchip-drm display-subsystem: [drm] fb0: rockchipdrmfb frame buffer device
```

`/dev/dri/card0` exists and **`card0-LVDS-1` reports `connected`** -- so the
VOP, our LVDS encoder patch and the PHY all work. The three patches that were
under suspicion for a day are exonerated twice over: once by the pristine
module blocking identically, and now by the whole path working once the genpd
deadlock is out of the way.

### What is left, and neither is the hang

- **The panel contributes no timing.** `card0-LVDS-1/modes` lists the generic
  fallbacks (1024x768, 800x600, ...) and not the panel's own 1024x600 at
  56.14 Hz. The `panel` device *is* bound to `panel-lvds`
  (`/sys/bus/platform/devices/panel/driver` points at it), so the driver
  attached but its `panel-timing` is not reaching the connector. That is the
  next thing to fix, and it is ordinary DT/driver work with a console to debug
  from.
- **Lima needs `gpu-sched`**, which was left out of the initramfs: `lima:
  Unknown symbol drm_sched_init`. A packaging omission, not a defect.

### The proper fix, rather than the boot argument

`fw_devlink=off` disables device links machine-wide, which is a diagnostic, not
a shipping configuration. The narrow fix is for the Rockchip power domains to
declare `GENPD_FLAG_NO_SYNC_STATE` -- the driver currently sets only
`GENPD_FLAG_PM_CLK | GENPD_FLAG_NO_STAY_ON` -- so the provider never runs the
sync_state that takes the domain lock while a consumer is trying to attach to
it. That is a one-line change to test, and if it holds it is worth sending
upstream with this stack trace attached.
