# Vendor blobs taken off this device

`8723cs.ko` — the Realtek RTL8723CS driver as shipped in the stock Android
system, 2 417 828 bytes, extracted 2026-09-01. It is kept here rather than
rebuilt because `CONFIG_MODVERSIONS=y` means a rebuilt kernel must reproduce
both the vermagic string and every symbol CRC before this module will load:

    vermagic    4.4.103 SMP preempt mod_unload modversions ARMv7 p2v8
    srcversion  9B93E0FDDCC00269C14A2A3
    BuildID     28981571a05a8ab13a3c77ceb5ffd15c8b976727

Running under the stock kernel, which is what this image does, both match by
construction. `CONFIG_MODULE_SIG` is unset, so signing is not a constraint.
