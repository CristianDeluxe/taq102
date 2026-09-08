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
