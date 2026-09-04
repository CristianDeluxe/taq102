#!/usr/bin/env python3
"""Graft what the 4.4.167 kernel needs onto the stock device tree.

The stock tree describes LVDS the way the 4.4.103 kernel wants it: a top-level
lvds@20038000 owning the registers, beside a separate mipi-dphy on the same
address. The 4.4.167 tree describes one shared video PHY at that address and
makes LVDS a child of the GRF that points at it, which is why the stock blob
boots this kernel but leaves "rockchip-lvds: failed to get phy: -19".

Grafting is safer than shipping the whole 4.4.167 tree, which reboot-loops:
everything else in the stock blob is known to boot this kernel already.
Endpoint phandles are carried over verbatim, because the VOP and the panel
reference them.

Two more one-line grafts, each of which cost a night to find:

- the PWM node's pinctrl state must be named "active", not "default";
  pwm-rockchip refuses to probe without it, and the whole display then fails as
  a chain of deferrals -- no PWM, no backlight, panel-simple deferring 390
  times a boot, LVDS at -517, and rockchip-drm binding nothing.
- the panel's bus-format 0x1009 is MEDIA_BUS_FMT_RGB666_1X18, a parallel
  format the LVDS driver's switch does not handle, so it falls through to
  VESA-24. This panel is JEIDA-24, which is 0x1012.
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src).read()


def node(body, start_pat):
    """Return (start, end) of the brace-balanced node whose header matches."""
    m = re.search(start_pat, body, re.M)
    if not m:
        raise SystemExit(f"no node matching {start_pat}")
    i = body.index("{", m.start())
    depth, j = 0, i
    while True:
        if body[j] == "{":
            depth += 1
        elif body[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    return m.start(), body.index(";", j) + 1


# 1. mipi-dphy -> the shared video PHY the new driver asks for, same phandle.
a, b = node(text, r"^\tmipi-dphy@20038000 \{")
video_phy = """\tvideo-phy@20038000 {
\t\tcompatible = "rockchip,rk3128-video-phy";
\t\treg = <0x20038000 0x4000 0x10110000 0x4000>;
\t\tclocks = <0x3 0x94 0x3 0x172 0x3 0x145 0x3 0x1d5>;
\t\tclock-names = "ref", "pclk_phy", "pclk_host", "h2p";
\t\tclock-output-names = "mipi_dphy_pll";
\t\t#clock-cells = <0x0>;
\t\tresets = <0x3 0x24>;
\t\treset-names = "rst";
\t\tpower-domains = <0x18 0x1>;
\t\trockchip,grf = <0xb>;
\t\t#phy-cells = <0x0>;
\t\tstatus = "okay";
\t\tlinux,phandle = <0x25>;
\t\tphandle = <0x25>;
\t};"""
text = text[:a] + video_phy + text[b:]

# 2. Lift the old LVDS node out, keeping its ports.
a, b = node(text, r"^\tlvds@20038000 \{")
old_lvds = text[a:b]
text = text[:a] + text[b:]

ports = old_lvds[old_lvds.index("\t\tports {") : old_lvds.rindex("\t};")]
ports = "\n".join("\t" + line if line.strip() else line for line in ports.split("\n"))

# 3. Re-attach it under the GRF, pointing at the PHY.
new_lvds = f"""
\t\tlvds {{
\t\t\tcompatible = "rockchip,rk3126-lvds";
\t\t\tphys = <0x25>;
\t\t\tphy-names = "phy";
\t\t\tstatus = "okay";
\t\t\tpinctrl-names = "lcdc";
\t\t\tpinctrl-0 = <0x34>;

{ports.rstrip()}
\t\t}};
"""
a, b = node(text, r"^\tsyscon@20008000 \{")
grf = text[a:b]
grf = grf[: grf.rindex("\t};")] + new_lvds.lstrip("\n") + "\t};"
text = text[:a] + grf + text[b:]

# 4. The PWM driver in this tree wants its pinctrl state called "active".
a, b = node(text, r"^\tpwm@20050000 \{")
pwm = text[a:b]
if 'pinctrl-names = "default";' not in pwm:
    raise SystemExit("pwm pinctrl-names not found: the 'active' graft would be lost")
text = text[:a] + pwm.replace('pinctrl-names = "default";',
                              'pinctrl-names = "active";', 1) + text[b:]

# 5. The panel is JEIDA-24 (0x1012), not RGB666_1X18 (0x1009).
if "\t\tbus-format = <0x1009>;" not in text:
    raise SystemExit("bus-format 0x1009 not found: the JEIDA-24 graft would be lost")
text = text.replace("\t\tbus-format = <0x1009>;", "\t\tbus-format = <0x1012>;", 1)

# 6. LDO6 of the RK816 powers the panel and nothing in the tree claims it. The
# stock node says only regulator-boot-on, so this kernel's regulator core
# switches the unused rail off at late init ("ldo6: disabling") and the panel
# goes deaf: white, and still white when the VOP is forced to emit black. The
# 4.4.103 kernel logs the same "disabling" and then "couldn't disable: -1" --
# a vendor hack in its regulator core is what kept this panel alive. Measured
# 2026-09-03: RK816 register 0x28 reads 0x73 on stock and 0xf1 here; setting
# bit 1 over i2c and re-running the modeset put the picture on the panel.
a, b = node(text, r"^\t\t\t\tLDO_REG6 \{")
ldo6 = text[a:b]
if "\t\t\t\t\tregulator-boot-on;\n" not in ldo6:
    raise SystemExit("LDO_REG6 regulator-boot-on not found: the always-on graft would be lost")
text = text[:a] + ldo6.replace("\t\t\t\t\tregulator-boot-on;\n",
                               "\t\t\t\t\tregulator-boot-on;\n\t\t\t\t\tregulator-always-on;\n",
                               1) + text[b:]

# 7. Experiment, off by default: TAQ102_NO_VOP_IOMMU=1 drops the VOP's iommus
# property, so rockchip-drm allocates contiguous CMA buffers instead of
# mapping pages through the VOP's IOMMU. Built 2026-09-04 to test whether the
# flicker I sees on every page flip -- flips to the same buffer do
# not flicker, flips between two identical buffers do -- is the per-frame
# page-table walk of a new address.
import os
if os.environ.get("TAQ102_NO_VOP_IOMMU"):
    a, b = node(text, r"^\tvop@1010e000 \{")
    vop = text[a:b]
    if "\t\tiommus = <0x22>;\n" not in vop:
        raise SystemExit("vop iommus = <0x22> not found: the no-iommu graft would be lost")
    text = text[:a] + vop.replace("\t\tiommus = <0x22>;\n", "", 1) + text[b:]

open(dst, "w").write(text)
print(f"wrote {dst}")
