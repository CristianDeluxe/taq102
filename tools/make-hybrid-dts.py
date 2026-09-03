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
text = text.replace(
    '\tpwm@20050000 {\n'
    '\t\tcompatible = "rockchip,rk3288-pwm";\n'
    '\t\treg = <0x20050000 0x10>;\n'
    '\t\t#pwm-cells = <0x3>;\n'
    '\t\tpinctrl-names = "default";',
    '\tpwm@20050000 {\n'
    '\t\tcompatible = "rockchip,rk3288-pwm";\n'
    '\t\treg = <0x20050000 0x10>;\n'
    '\t\t#pwm-cells = <0x3>;\n'
    '\t\tpinctrl-names = "active";', 1)

# 5. The panel is JEIDA-24 (0x1012), not RGB666_1X18 (0x1009).
text = text.replace("\t\tbus-format = <0x1009>;", "\t\tbus-format = <0x1012>;", 1)

open(dst, "w").write(text)
print(f"wrote {dst}")
