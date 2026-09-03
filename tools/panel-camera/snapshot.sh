#!/bin/sh
# Capture everything the running kernel exposes about the display path without
# /dev/mem, into one directory, so two kernels can be diffed file by file.
#   snapshot.sh <outdir>
# The stock 4.4.103 has CONFIG_DEVMEM unset, so debugfs and sysfs are the only
# windows into it: regulators, the PMIC regmap, pinmux, gpio, clocks, power
# domains, pwm and the DRM summary. Every file is captured even when a kernel
# lacks it, and a missing one is itself a difference worth seeing.
set -eu
here="$(dirname "$0")"
out="$1"
mkdir -p "$out"

grab() {
	name="$1"
	shift
	"$here/tablet.sh" "$*" >"$out/$name" 2>&1 || true
}

grab uname 'uname -a; cat /proc/version; cat /proc/cmdline'
grab dmesg 'dmesg'
grab iomem 'cat /proc/iomem'
grab interrupts 'cat /proc/interrupts'
grab mounts 'mount -t debugfs none /sys/kernel/debug 2>/dev/null; mount'
grab regulator_summary 'cat /sys/kernel/debug/regulator/regulator_summary'
grab regulator_supply_map 'cat /sys/kernel/debug/regulator/supply_map'
grab regulator_class 'for r in /sys/class/regulator/*; do echo "== $r $(cat $r/name 2>/dev/null)"; for f in state status microvolts min_microvolts max_microvolts num_users type; do printf "%s=%s\n" $f "$(cat $r/$f 2>/dev/null)"; done; done'
grab regmap_list 'ls -la /sys/kernel/debug/regmap/'
grab regmap_all 'for d in /sys/kernel/debug/regmap/*; do echo "== $d"; cat $d/name 2>/dev/null; cat $d/registers 2>/dev/null; done'
grab pinmux_pins 'cat /sys/kernel/debug/pinctrl/*/pinmux-pins'
grab pinconf_pins 'cat /sys/kernel/debug/pinctrl/*/pinconf-pins'
grab pinctrl_handles 'cat /sys/kernel/debug/pinctrl/pinctrl-handles'
grab pinctrl_maps 'cat /sys/kernel/debug/pinctrl/pinctrl-maps'
grab gpio 'cat /sys/kernel/debug/gpio'
grab clk_summary 'cat /sys/kernel/debug/clk/clk_summary'
grab pm_genpd 'cat /sys/kernel/debug/pm_genpd/pm_genpd_summary'
grab pwm 'cat /sys/kernel/debug/pwm'
grab dri_summary 'cat /sys/kernel/debug/dri/0/summary'
grab dri_lvds 'cat /sys/kernel/debug/dri/0/LVDS-1/* 2>/dev/null; ls /sys/kernel/debug/dri/0/LVDS-1'
grab backlight 'for b in /sys/class/backlight/*; do echo "== $b"; for f in brightness actual_brightness max_brightness bl_power; do printf "%s=%s\n" $f "$(cat $b/$f)"; done; done'
grab i2c 'ls -la /sys/bus/i2c/devices/; for d in /sys/bus/i2c/devices/*; do echo "== $d $(cat $d/name 2>/dev/null) $(cat $d/modalias 2>/dev/null)"; done'
grab platform_drivers 'ls /sys/bus/platform/drivers/; echo; for d in /sys/bus/platform/drivers/*; do printf "%s: " ${d##*/}; ls $d | grep -v -E "^(bind|unbind|uevent|module)$" | tr "\n" " "; echo; done'
grab devices_display 'for d in /sys/bus/platform/devices/*vop* /sys/bus/platform/devices/*lvds* /sys/bus/platform/devices/*phy* /sys/bus/platform/devices/*mipi* /sys/bus/platform/devices/*panel* /sys/bus/platform/devices/*backlight* /sys/bus/platform/devices/*pwm* /sys/bus/platform/devices/display-subsystem; do [ -e "$d" ] || continue; echo "== $d -> $(readlink $d/driver 2>/dev/null)"; done'
grab dt_display 'for n in /proc/device-tree/lvds* /proc/device-tree/*/lvds* /proc/device-tree/panel* /proc/device-tree/*/panel* /proc/device-tree/backlight* /proc/device-tree/display-subsystem; do [ -e "$n" ] || continue; echo "== $n"; for p in $n/*; do [ -f "$p" ] || continue; printf "%s: " ${p##*/}; od -An -tx1 -v "$p" | tr -d "\n" | head -c 400; echo; done; done'

ls -la "$out"
