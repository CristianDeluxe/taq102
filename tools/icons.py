#!/usr/bin/env python3
"""The desk's icons, drawn as vectors at 4x and rasterised into 8-bit alpha
bitmaps the painter blends in any colour. Regenerate `src/icon_data.h`:

    python3 tools/icons.py > src/icon_data.h

An 18 px optical square leaves 3 px breathing room inside the 24 px grid;
a 2 px stroke stays legible on the tablet without overpowering the text.
The gear's dense disc is 10% smaller (16.2 px); the short battery is 2 px
wider (20 px including its terminal) and 12 px tall to balance their weight.
The Wi-Fi dot and three arcs share a centre and 4.5 px radial spacing, with
a 70 degree sweep and round ends. BOX downsampling averages the 4x coverage
without a ringing halo, so nonzero alpha stays within one pixel of the optical
box even for the wider battery. All positions at call sites stay unchanged.
"""
import math

from PIL import Image, ImageDraw

S = 4  # supersample
OPTICAL_SIZE = 18
STROKE = 2


def canvas(w, h):
    im = Image.new("L", (w * S, h * S), 0)
    return im, ImageDraw.Draw(im)


def gear(size=24):
    im, d = canvas(size, size)
    cx = cy = size / 2
    r = OPTICAL_SIZE * 0.9 / 2
    pts = []
    for tooth in range(8):
        for angle, radius in [(-22.5, r - STROKE), (-10, r),
                              (10, r), (22.5, r - STROKE)]:
            a = math.radians(tooth * 45 + angle)
            pts.append(((cx + math.cos(a) * radius) * S,
                        (cy + math.sin(a) * radius) * S))
    d.polygon(pts, fill=255)
    hole = r - 2 * STROKE
    d.ellipse([(cx - hole) * S, (cy - hole) * S,
               (cx + hole) * S, (cy + hole) * S], fill=0)
    return im


def lock(size=24, open_=False):
    im, d = canvas(size, size)
    cx = cy = size / 2
    top = cy - OPTICAL_SIZE / 2 + STROKE / 2
    bottom = cy + OPTICAL_SIZE / 2 - STROKE / 2
    half = OPTICAL_SIZE / 3
    body_y = cy - STROKE / 2
    d.rounded_rectangle([(cx - half) * S, body_y * S,
                         (cx + half) * S, bottom * S],
                        radius=STROKE * S, outline=255, width=STROKE * S)
    sh = OPTICAL_SIZE / 2
    x0 = cx - sh / 2 - (STROKE if open_ else 0)
    x1 = x0 + sh
    d.arc([x0 * S, top * S, x1 * S, (top + sh) * S],
          180, 360, fill=255, width=STROKE * S)
    middle = top + sh / 2
    d.line([x0 * S, middle * S, x0 * S, body_y * S],
           fill=255, width=STROKE * S)
    if not open_:
        d.line([(x1 - STROKE / 2) * S, middle * S,
                (x1 - STROKE / 2) * S, body_y * S],
               fill=255, width=STROKE * S)
    return im


def wifi_arc(size=24, index=0):
    """One separately lit fan layer, balanced against the battery and lock.

    A 4 px filled apex replaces the speck. Rings at 5, 9 and 13 px leave
    the same 2 px radial gap from the apex and between strokes. Raising the
    common centre 2 px puts the fan's lower edge beside the battery's.
    """
    im, d = canvas(size, size)
    cx, cy = size / 2, size / 2 + OPTICAL_SIZE / 2 - 2 * STROKE
    radius = STROKE / 2
    if index == 0:
        # Pillow includes both ellipse endpoints; subtract half a sample so
        # the nominal 4 px diameter covers exactly sixteen high-res samples.
        radius = STROKE - 1 / (2 * S)
        d.ellipse([(cx - radius) * S, (cy - radius) * S,
                   (cx + radius) * S, (cy + radius) * S], fill=255)
        return im
    rr = STROKE / 2 + index * (OPTICAL_SIZE - 3 * STROKE) / 3
    pts = []
    for degree in range(235, 306):
        angle = math.radians(degree)
        pts.append(((cx + rr * math.cos(angle)) * S,
                    (cy + rr * math.sin(angle)) * S))
    d.line(pts, fill=255, width=STROKE * S, joint="curve")
    for x, y in [pts[0], pts[-1]]:
        d.ellipse([x - radius * S, y - radius * S,
                   x + radius * S, y + radius * S], fill=255)
    return im


def battery_shell(size=24):
    im, d = canvas(size, size)
    w, h = OPTICAL_SIZE + STROKE, OPTICAL_SIZE * 2 / 3
    x0, y0 = (size - w) / 2, (size - h) / 2
    x1 = x0 + w - STROKE
    d.rounded_rectangle([x0 * S, y0 * S, x1 * S, (y0 + h) * S],
                        radius=STROKE * S, outline=255, width=STROKE * S)
    d.rounded_rectangle([x1 * S, (y0 + h / 3) * S,
                         (x1 + STROKE) * S, (y0 + 2 * h / 3) * S],
                        radius=STROKE * S // 2, fill=255)
    return im


def battery_cell(size=24):
    """The inside of the shell, a full cell; the painter clips it by level."""
    im, d = canvas(size, size)
    w, h = OPTICAL_SIZE + STROKE, OPTICAL_SIZE * 2 / 3
    x0, y0 = (size - w) / 2, (size - h) / 2
    inset = STROKE * 1.5
    d.rounded_rectangle([(x0 + inset) * S, (y0 + inset) * S,
                         (x0 + w - STROKE - inset) * S, (y0 + h - inset) * S],
                        radius=STROKE * S // 2, fill=255)
    return im


def bolt(size=24):
    im, d = canvas(size, size)
    cx, cy, r = size / 2, size / 2, OPTICAL_SIZE / 2
    pts = [(cx + STROKE, cy - r), (cx - r / 2, cy + STROKE / 2),
           (cx, cy + STROKE / 2), (cx - STROKE, cy + r),
           (cx + r / 2, cy - STROKE / 2), (cx, cy - STROKE / 2)]
    d.polygon([(px * S, py * S) for px, py in pts], fill=255)
    return im


def check(size=24):
    im, d = canvas(size, size)
    cx, cy = size / 2, size / 2
    r = (OPTICAL_SIZE - STROKE) / 2
    d.line([(cx - r) * S, cy * S, (cx - STROKE) * S, (cy + r / 2) * S,
            (cx + r) * S, (cy - r / 2) * S],
           fill=255, width=STROKE * S, joint="curve")
    return im


def chevron(size=24):
    im, d = canvas(size, size)
    cx, cy = size / 2, size / 2
    r = (OPTICAL_SIZE - STROKE) / 2
    d.line([(cx - r / 3) * S, (cy - r) * S, (cx + r / 3) * S, cy * S,
            (cx - r / 3) * S, (cy + r) * S],
           fill=255, width=STROKE * S, joint="curve")
    return im


ICONS = [
    ("ICON_GEAR", gear()),
    ("ICON_LOCK", lock()),
    ("ICON_LOCK_OPEN", lock(open_=True)),
    ("ICON_WIFI_0", wifi_arc(index=0)),
    ("ICON_WIFI_1", wifi_arc(index=1)),
    ("ICON_WIFI_2", wifi_arc(index=2)),
    ("ICON_WIFI_3", wifi_arc(index=3)),
    ("ICON_BATTERY", battery_shell()),
    ("ICON_BATTERY_CELL", battery_cell()),
    ("ICON_BOLT", bolt()),
    ("ICON_CHECK", check()),
    ("ICON_CHEVRON", chevron()),
]


def emit():
    print("// Generated by tools/icons.py: 24 px icons as 8-bit alpha, row-major.")
    print("// Do not edit; change the vectors and regenerate.")
    print("#ifndef ICON_DATA_H\n#define ICON_DATA_H\n#include <stdint.h>\n")
    print("#define ICON_SIZE 24")
    print("enum icon_id {")
    for name, _ in ICONS:
        print(f"    {name},")
    print("    ICON_COUNT\n};\n")
    print("static const uint8_t ICON_ALPHA[ICON_COUNT][ICON_SIZE * ICON_SIZE] = {")
    for name, im in ICONS:
        small = im.resize((im.width // S, im.height // S), Image.Resampling.BOX)
        data = list(small.tobytes())
        print(f"    [{name}] = {{")
        for row in range(24):
            vals = ", ".join(f"{v:3d}" for v in data[row * 24:(row + 1) * 24])
            print(f"        {vals},")
        print("    },")
    print("};\n\n#endif")


if __name__ == "__main__":
    emit()
