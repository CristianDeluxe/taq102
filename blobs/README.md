# Vendor binaries this device needs

None of these files are in the repository. They are either extracted from the
tablet you own or built from source, and the build reads them from this
directory. `tools/build-kernel.sh` produces the two that are built here;
the first has to come off your own device.

## `8723cs-4.4.103.ko` — extracted, not redistributed

The Realtek RTL8723CS driver as shipped in the stock Android system. It is a
binary because `CONFIG_MODVERSIONS=y`: any kernel meant to load it must
reproduce both the vermagic string and every symbol CRC.

    vermagic    4.4.103 SMP preempt mod_unload modversions ARMv7 p2v8
    srcversion  9B93E0FDDCC00269C14A2A3

Under the stock kernel, which is what `boot` runs, both match by construction.
Pull it from a device running the stock system:

    adb pull /system/lib/modules/8723cs.ko blobs/8723cs-4.4.103.ko

It is a derivative work of the Linux kernel distributed by the vendor without
corresponding source, so this project does not redistribute it.

## `8723cs-4.4.167.ko` — built from source

The same driver built against our own kernel (`54shady/qop_kernel`, 4.4.167),
so the vermagic question does not arise:

    vermagic    4.4.167 SMP preempt mod_unload modversions ARMv7 p2v8
    srcversion  AC99859889E65B1099082FD

The image carries both and `taq102-wifi` picks by `uname -r`, so one image
boots on either kernel. `CONFIG_MODULE_SIG` is unset, so signing is not a
constraint.

## `phy-rockchip-inno-video-combo-phy-4.4.167.ko` — built from source

The LVDS video PHY driver built from our 4.4.167 tree with `kernel/patches/0001`,
`0002` and `0007` applied: the vendor's 336 MHz divider pair, the analog block
powered in LVDS mode, and a reset pulse before programming. It is a module
because built into the kernel it hangs the boot at the PHY's first power-on.
`/init` loads it through `taq102-display`.

    120536 bytes, vermagic 4.4.167 SMP preempt mod_unload modversions ARMv7 p2v8

Verify the size after any copy: a zero-byte transfer once read as
`invalid module format`.

## Touchscreen firmware

`br2-external/board/taq102/rootfs-overlay-mainline/lib/firmware/silead/gsl3673.fw`
is proprietary Silead firmware and is not redistributed either. Extract it from
the stock kernel image of your own device; the format and the extraction are
described in `docs/research/`. The array this tablet needs has 4719 records and
a resolution word of 0x02580400 — the one in the vendor tree belongs to a
different panel and reports no touch.
