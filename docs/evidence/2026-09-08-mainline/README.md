# Evidence, 2026-09-08: mainline on the TAQ-102

Read `kernel/mainline/FINDINGS.md` first; these are the files it cites.

| file | what it shows |
| --- | --- |
| `first-mainline-boot-console.log` | the first mainline boot to reach userspace (v59): arch timer, both memory banks, RK816 battery, eMMC and `/data`, rtw88 associating, the ACM console |
| `highlights.txt` | the same boot, filtered to the lines that matter |
| `genpd-deadlock-stack.txt` | the SysRq-w trace of `insmod` blocked in `genpd_add_device` -- mainline's own PHY module, none of our patches |
| `drm-first-bind-fw_devlink-off.log` | `fw_devlink=off` clears it: PHY loads, VOP and lvds bind, DRM initialises, `/dev/dri/card0` appears |
| `drm-comes-up-with-fw_devlink-off.txt` | the same, cut to the six lines that say it |
| `lvds-panel-bridge-bug.txt` | `panel=4fcc1c99` at bind, `panel=00000000` at get_modes -- the LVDS driver hiding its own panel |
| `panel-working-console.log` | after the fix: connector offers 1024x600 only, fbcon resizes to 128x37, console visible on the tablet's screen |
| `vendor-phy-clock-sequence.txt` | the vendor PHY enabling four clocks where mainline names two; the h2p hypothesis came from here and turned out not to be the cause |
