# Binaries that belong to this device

`8723cs-4.4.103.ko` — the Realtek RTL8723CS driver as shipped in the stock
Android system, extracted 2026-09-01. It is kept as a binary because
`CONFIG_MODVERSIONS=y` means any kernel meant to load it must reproduce both the
vermagic string and every symbol CRC:

    vermagic    4.4.103 SMP preempt mod_unload modversions ARMv7 p2v8
    srcversion  9B93E0FDDCC00269C14A2A3

Under the stock kernel, which is what `boot` runs, both match by construction.

`8723cs-4.4.167.ko` — the same driver **built from source** against our own
kernel (`54shady/qop_kernel`, 4.4.167), so the vermagic question does not arise:

    vermagic    4.4.167 SMP preempt mod_unload modversions ARMv7 p2v8
    srcversion  AC99859889E65B1099082FD

The image carries both and `taq102-wifi` picks by `uname -r`, so one image boots
on either kernel. `CONFIG_MODULE_SIG` is unset, so signing is not a constraint.

`phy-rockchip-inno-video-combo-phy-4.4.167.ko` — the LVDS video PHY driver
built from our 4.4.167 tree with `kernel/patches/0002` applied, kept as a
module because built into the kernel it hangs the boot at the PHY's first
power-on. Loaded by `taq102-display` from `/init`. md5
`8082e7153743e4294a4e9c6c9acece76`, 120104 bytes; verify the size after any
copy, a zero-byte transfer once read as `invalid module format`.

    vermagic    4.4.167 SMP preempt mod_unload modversions ARMv7 p2v8
