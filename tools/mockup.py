#!/usr/bin/env python3
"""Mockups of the desk's second design, drawn at 2x and downsampled, so the
composition can be judged before a line of C moves. The
geometry here is the geometry the painter will get: every number is on an
8 px grid and every text is measured with the bundled Inter.

    python3 tools/mockup.py out_dir
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent.parent
FONTS = HERE / "br2-external/package/taq102-fonts/fonts"
W, H, S = 1024, 600, 2  # panel, supersample

# Palette: one dark ground, one tile, one raised tile, ink, muted, amber for
# "the master says running", red for the stop, and the show's own colours.
BG = (14, 14, 16)
TILE = (30, 30, 34)
RAISED = (42, 42, 48)
LINE = (52, 52, 58)
INK = (245, 245, 247)
MUTED = (140, 140, 150)
AMBER = (224, 138, 0)
AMBER_INK = (20, 20, 20)
RED = (192, 48, 32)
HOLD = (36, 32, 44)      # a momentary button is its own kind of dark
HOLD_LINE = (120, 96, 200)


def font(name, px):
    return ImageFont.truetype(str(FONTS / name), px * S)


F_TAB = font("Inter-SemiBold.ttf", 15)
F_SECTION = font("Inter-SemiBold.ttf", 12)
F_TILE = font("Inter-SemiBold.ttf", 17)
F_TILE_S = font("Inter-SemiBold.ttf", 14)
F_SUB = font("Inter-Regular.ttf", 13)
F_BIG = font("Inter-SemiBold.ttf", 44)
F_SMALL = font("Inter-Regular.ttf", 12)
F_LABEL = font("Inter-Regular.ttf", 15)


class Canvas:
    def __init__(self):
        self.im = Image.new("RGB", (W * S, H * S), BG)
        self.d = ImageDraw.Draw(self.im)

    def rect(self, x, y, w, h, fill, r=10, outline=None, width=1):
        self.d.rounded_rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], radius=r * S,
                                 fill=fill, outline=outline, width=width * S)

    def text(self, x, y, s, f, col, anchor="la", max_w=None):
        if max_w is not None:
            while self.d.textlength(s, font=f) > max_w * S and len(s) > 1:
                s = s[:-2] + "…"
        self.d.text((x * S, y * S), s, font=f, fill=col, anchor=anchor)

    def centred(self, x, y, w, h, s, f, col, dy=0):
        self.d.text(((x + w / 2) * S, (y + h / 2 + dy) * S), s, font=f, fill=col, anchor="mm")

    def two_lines(self, x, y, w, h, s, f, col, gap=18):
        words = s.split()
        if self.d.textlength(s, font=f) <= (w - 16) * S or len(words) < 2:
            self.centred(x, y, w, h, s, f, col)
            return
        best = None
        for i in range(1, len(words)):
            a, b = " ".join(words[:i]), " ".join(words[i:])
            wa, wb = self.d.textlength(a, font=f), self.d.textlength(b, font=f)
            if max(wa, wb) <= (w - 16) * S and (best is None or max(wa, wb) < best[0]):
                best = (max(wa, wb), a, b)
        if not best:
            self.centred(x, y, w, h, s, f, col)
            return
        self.centred(x, y, w, h, best[1], f, col, dy=-gap / 2)
        self.centred(x, y, w, h, best[2], f, col, dy=gap / 2)

    def circle(self, cx, cy, r, fill, outline=None, width=1):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=fill,
                       outline=outline, width=width * S)

    def save(self, path):
        self.im.resize((W, H), Image.LANCZOS).save(path)


# ---- icons, drawn as vectors here; the C side gets them as alpha bitmaps.
def icon_gear(c, cx, cy, r, col):
    import math
    d = c.d
    pts = []
    n = 8
    for i in range(n * 2):
        a = math.pi * 2 * i / (n * 2) - math.pi / 2
        rr = r if i % 2 == 0 else r * 0.72
        pts.append(((cx + math.cos(a) * rr) * S, (cy + math.sin(a) * rr) * S))
    d.polygon(pts, fill=col)
    c.circle(cx, cy, r * 0.34, BG)


def icon_lock(c, cx, cy, size, col, open_=False):
    b = size
    c.rect(cx - b / 2, cy - b * 0.1, b, b * 0.6, col, r=3)
    d = c.d
    d.arc([(cx - b * 0.32) * S, (cy - b * 0.62) * S, (cx + b * 0.32) * S, (cy + b * 0.02) * S],
          200 if open_ else 180, 360, fill=col, width=int(b * 0.14 * S))


def icon_wifi(c, cx, cy, size, col, bars=3):
    d = c.d
    for i in range(3):
        rr = size * (0.35 + i * 0.32)
        colr = col if i < bars else LINE
        d.arc([(cx - rr) * S, (cy - rr) * S, (cx + rr) * S, (cy + rr) * S], 225, 315, fill=colr,
              width=int(size * 0.14 * S))
    c.circle(cx, cy, size * 0.12, col)


def icon_battery(c, cx, cy, size, col, level=1.0, charging=False):
    w, h = size * 1.6, size * 0.8
    c.rect(cx - w / 2, cy - h / 2, w, h, None, r=3, outline=col, width=2)
    c.rect(cx + w / 2 + 1, cy - h / 6, 3, h / 3, col, r=1)
    c.rect(cx - w / 2 + 3, cy - h / 2 + 3, (w - 6) * level, h - 6, col, r=2)
    if charging:
        d = c.d
        pts = [(cx + 1, cy - h * 0.42), (cx - 3, cy + 1), (cx + 1, cy + 1), (cx - 1, cy + h * 0.42),
               (cx + 3, cy - 1), (cx - 1, cy - 1)]
        d.polygon([(px * S, py * S) for px, py in pts], fill=BG)


def icon_bolt(c, cx, cy, size, col):
    d = c.d
    s = size
    pts = [(cx + s * 0.1, cy - s), (cx - s * 0.5, cy + s * 0.1), (cx - s * 0.05, cy + s * 0.1),
           (cx - s * 0.1, cy + s), (cx + s * 0.5, cy - s * 0.1), (cx + s * 0.05, cy - s * 0.1)]
    d.polygon([(px * S, py * S) for px, py in pts], fill=col)


# ---- chrome shared by every page
TABS = ["SHOW", "COLOR", "PIXELES", "CABEZAS", "GOBOS", "PRISMA", "SPEED"]
MASTER_X = 860


def chrome(c, active, linked=True, master=100):
    # Top bar: tabs left, status right.
    x = 16
    for i, t in enumerate(TABS):
        w = int(c.d.textlength(t, font=F_TAB) / S) + 28
        if i == active:
            c.rect(x, 8, w, 32, RAISED, r=8)
            c.text(x + w / 2, 24, t, F_TAB, INK, anchor="mm")
        else:
            c.text(x + w / 2, 24, t, F_TAB, MUTED, anchor="mm")
        x += w + 4
    # Right side of the bar: link word, wifi, battery, gear.
    c.text(770, 24, "Vibra · 192.168.1.65" if linked else "Sin master", F_SMALL,
           MUTED if linked else RED, anchor="rm")
    icon_wifi(c, 800, 30, 14, INK, bars=3)
    icon_battery(c, 852, 24, 14, INK, level=1.0, charging=True)
    c.text(874, 24, "100%", F_SMALL, MUTED, anchor="lm")
    icon_gear(c, 936, 24, 12, MUTED)
    icon_lock(c, 984, 24, 18, MUTED, open_=True)
    # Master column: fader with the readout on a backing, the stop below.
    c.rect(MASTER_X, 56, 148, 372, TILE, r=14)
    fill_h = int(372 * master / 100)
    c.rect(MASTER_X, 56 + 372 - fill_h, 148, fill_h, AMBER, r=14)
    c.rect(MASTER_X + 16, 216, 116, 52, (0, 0, 0), r=10)
    c.centred(MASTER_X + 16, 216, 116, 52, f"{master}%", F_BIG.font_variant(size=30 * S), INK)
    c.text(MASTER_X + 74, 72, "MASTER", F_SECTION, AMBER_INK if master > 90 else MUTED, anchor="mm")
    c.rect(MASTER_X, 440, 148, 144, RED, r=14)
    c.centred(MASTER_X, 440, 148, 144, "PARAR", F_TILE, INK, dy=-22)
    c.centred(MASTER_X, 440, 148, 144, "TODO", F_TILE, INK, dy=0)
    c.centred(MASTER_X, 440, 148, 144, "1.0 s fade", F_SMALL, (255, 200, 190), dy=30)


def section(c, x, y, title, w=828):
    c.text(x, y, title.upper(), F_SECTION, MUTED)
    c.d.line([(x + c.d.textlength(title.upper(), font=F_SECTION) / S + 10) * S, (y + 8) * S,
              (x + w) * S, (y + 8) * S], fill=LINE, width=S)


def state_tile(c, x, y, w, h, name, sub, on=False, big=False):
    c.rect(x, y, w, h, AMBER if on else TILE, r=12)
    f = F_TILE if big or len(name) <= 8 else F_TILE_S
    if sub:
        c.two_lines(x, y - 8, w, h, name, f, AMBER_INK if on else INK)
        c.centred(x, y + 14, w, h, sub, F_SMALL, AMBER_INK if on else MUTED)
    else:
        c.two_lines(x, y, w, h, name, f, AMBER_INK if on else INK, gap=17)


def hold_tile(c, x, y, w, h, name, pressed=False, cap="3 s"):
    c.rect(x, y, w, h, RAISED if pressed else HOLD, r=12, outline=INK if pressed else HOLD_LINE,
           width=3 if pressed else 1)
    icon_bolt(c, x + 20, y + 20, 9, HOLD_LINE if not pressed else INK)
    c.two_lines(c_x := x, y + 8, w, h, name, F_TILE_S, INK)
    c.text(x + w - 12, y + h - 12, f"mantener · {cap}", F_SMALL, MUTED, anchor="rb")


def swatch(c, cx, cy, r, colours, on=False, label=None):
    n = len(colours)
    d = c.d
    for i, col in enumerate(colours):
        d.pieslice([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S],
                   -90 + 360 * i / n, -90 + 360 * (i + 1) / n, fill=col)
    if on:
        c.circle(cx, cy, r + 5, None, outline=AMBER, width=3)
    else:
        c.circle(cx, cy, r * 0.55, TILE)
    if label:
        c.text(cx, cy + r + 14, label, F_SMALL, INK, anchor="mm")


def page_show(path):
    c = Canvas()
    chrome(c, 0)
    x0 = 16
    # Row 1: the room's state. Seven across, AUTO first and lit.
    section(c, x0, 56, "La sala está así")
    names = [("AUTO", "el show se lleva solo"), ("CHARLA", "alguien habla"), ("TRANQUILO", "bajón"),
             ("FIESTA", "marcha normal"), ("LOCURA", "todo a la vez"), ("BLANCO TOTAL", "luz de trabajo"),
             ("TODO NEGRO", "look a negro")]
    tw = (828 - 6 * 8) // 7
    for i, (n, s) in enumerate(names):
        state_tile(c, x0 + i * (tw + 8), 76, tw, 88, n, s, on=i == 0)
    # Row 2: hits held while the finger is down, the most used thing at a show.
    section(c, x0, 180, "Golpes · mantener pulsado")
    hits = ["FLASH", "FLASH LENTO", "FLASH COLOR", "STROBO", "STROBO SUAVE"]
    hw = (828 - 5 * 8 - 128) // 5
    for i, n in enumerate(hits):
        hold_tile(c, x0 + i * (hw + 8), 200, hw, 104, n, pressed=i == 0)
    # COLOR BEAM is a toggle in the same row, the one that latches.
    state_tile(c, x0 + 5 * (hw + 8), 200, 128, 104, "COLOR BEAM", "", on=False)
    # Row 3: smoke. Two holds with a short cap, the ambient rhythm as one control.
    section(c, x0, 320, "Humo")
    hold_tile(c, x0, 340, 160, 88, "HUMO YA", cap="1.5 s")
    hold_tile(c, x0 + 168, 340, 160, 88, "HUMO VERT", cap="1.5 s")
    seg_x = x0 + 344
    c.text(seg_x, 340, "AMBIENTE", F_SECTION, MUTED)
    opts = ["OFF", "1 min", "2 min", "4 min", "8 min"]
    sw_ = (828 - 344 - 4 * 4) // 5
    for i, o in enumerate(opts):
        on = i == 2
        c.rect(seg_x + i * (sw_ + 4), 360, sw_, 68, AMBER if on else TILE, r=10)
        c.centred(seg_x + i * (sw_ + 4), 360, sw_, 68, o, F_TILE_S, AMBER_INK if on else INK)
    # Row 4: the twelve rig colours, a tempo readout and tap.
    section(c, x0, 444, "Color del rig")
    cols = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (150, 0, 255), (255, 255, 0), (0, 255, 255),
            (255, 0, 255), (255, 255, 255), (255, 120, 0), (255, 0, 128)]
    labels = ["Rojo", "Verde", "Azul", "UV", "Amarillo", "Cyan", "Magenta", "Blanco", "Naranja", "Rosa"]
    for i, (col, lb) in enumerate(zip(cols, labels)):
        cx = x0 + 28 + i * 60
        swatch(c, cx, 494, 20, [col], on=i == 2, label=lb)
    # Tempo, at the right of the colours: the base BPM and a tap target.
    c.rect(x0 + 620, 464, 208, 104, TILE, r=12)
    c.text(x0 + 636, 480, "TEMPO SHOW", F_SECTION, MUTED)
    c.text(x0 + 636, 500, "120", F_BIG.font_variant(size=34 * S), INK)
    c.text(x0 + 706, 528, "BPM", F_SMALL, MUTED)
    c.rect(x0 + 744, 476, 72, 80, RAISED, r=10)
    c.centred(x0 + 744, 476, 72, 80, "TAP", F_TILE_S, INK)
    c.save(path)


def page_color(path):
    c = Canvas()
    chrome(c, 1)
    x0 = 16
    section(c, x0, 56, "Automático")
    hooks = ["AUTO colores", "Mezcla", "Luz Charla"]
    for i, n in enumerate(hooks):
        state_tile(c, x0 + i * 208, 76, 200, 64, n, "", on=i == 0)
    section(c, x0, 156, "Elegir · rig")
    rig = [("Rojo", [(255, 0, 0)]), ("Verde", [(0, 255, 0)]), ("Azul", [(0, 0, 255)]),
           ("Ultravioleta", [(150, 0, 255)]), ("Amarillo", [(255, 255, 0)]), ("Cyan", [(0, 255, 255)]),
           ("Magenta", [(255, 0, 255)]), ("Blanco", [(255, 255, 255)]), ("Naranja", [(255, 120, 0)]),
           ("Rosa", [(255, 0, 128)]), ("Multicolor 1", [(255, 0, 0), (0, 255, 0), (0, 0, 255), (150, 0, 255)]),
           ("Multicolor 2", [(255, 255, 0), (0, 255, 255), (255, 0, 255), (255, 120, 0)]),
           ("4 Colores 1", [(0, 0, 255), (255, 255, 255), (255, 0, 0), (0, 255, 0)]),
           ("4 Colores 2", [(0, 255, 0), (0, 0, 255), (255, 0, 0), (255, 255, 255)]),
           ("4 Colores 3", [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]),
           ("4 Colores 4", [(0, 0, 255), (255, 0, 0), (255, 255, 255), (0, 255, 0)]),
           ("Arcoiris junto", None), ("Arcoiris fases", None)]
    tw = (828 - 5 * 8) // 6
    for i, (n, cols) in enumerate(rig):
        col, row = i % 6, i // 6
        x, y = x0 + col * (tw + 8), 176 + row * 96
        on = i == 2
        c.rect(x, y, tw, 88, AMBER if on else TILE, r=12)
        if cols:
            swatch(c, x + 30, y + 44, 16, cols, on=False)
            c.rect(x + 22, y + 36, 16, 16, AMBER if on else TILE, r=8)
        c.two_lines(x + 52, y, tw - 60, 88, n, F_TILE_S, AMBER_INK if on else INK, gap=17)
    section(c, x0, 468, "Elegir · cabezas")
    heads = [("Rojo", [(255, 0, 0)]), ("Azul", [(0, 0, 255)]), ("Magenta", [(255, 0, 255)]),
             ("Amarillo", [(255, 255, 0)]), ("Blanco", [(255, 255, 255)])]
    for i, (n, cols) in enumerate(heads):
        x, y = x0 + i * (tw + 8), 488
        c.rect(x, y, tw, 88, TILE, r=12)
        swatch(c, x + 30, y + 44, 16, cols)
        c.two_lines(x + 52, y, tw - 60, 88, n, F_TILE_S, INK)
    # The golpes of this family live in SHOW; here a quiet pointer.
    c.text(x0 + 828, 470, "golpes de color: en SHOW", F_SMALL, MUTED, anchor="ra")
    c.save(path)


def page_setup(path):
    c = Canvas()
    chrome(c, -1)
    x0 = 16
    c.rect(x0, 56, 828, 528, TILE, r=14)
    c.text(x0 + 24, 76, "AJUSTES", F_SECTION, MUTED)
    c.rect(x0 + 828 - 96, 68, 80, 36, RAISED, r=8)
    c.centred(x0 + 828 - 96, 68, 80, 36, "Cerrar", F_TILE_S, INK)
    # Two columns: network and master; footer: brightness and battery.
    cw = (828 - 48 - 24) // 2
    for i, title in enumerate(["WI-FI", "MASTER QLC+"]):
        cx = x0 + 24 + i * (cw + 24)
        c.text(cx, 116, title, F_SECTION, MUTED)
        c.d.line([cx * S, 132 * S, (cx + cw) * S, 132 * S], fill=LINE, width=S)
    cx = x0 + 24
    c.text(cx, 148, "TestNet", F_TILE, INK)
    c.text(cx, 172, "192.168.1.71 · conectado", F_SMALL, MUTED)
    nets = [("ArloBase-0000000000", 4, ""), ("TestNet5", 4, ""), ("SUN2000-HV0000", 3, ""),
            ("MOVISTAR-WIFI6-C2B0", 3, ""), ("eduroam", 2, "empresa")]
    for i, (n, bars, tag) in enumerate(nets):
        y = 200 + i * 52
        c.rect(cx, y, cw, 44, RAISED, r=10)
        icon_wifi(c, cx + 24, y + 28, 12, INK, bars=bars)
        c.text(cx + 48, y + 22, n, F_LABEL, INK if not tag else MUTED, anchor="lm", max_w=cw - 120)
        if tag:
            c.text(cx + cw - 12, y + 22, tag, F_SMALL, MUTED, anchor="rm")
    c.rect(cx, 476, cw, 44, INK, r=10)
    c.centred(cx, 476, cw, 44, "Buscar redes", F_TILE_S, BG)
    cx = x0 + 24 + cw + 24
    c.text(cx, 148, "192.168.1.65 : 9998", F_TILE, INK)
    c.text(cx, 172, "Vibra · enlazado · 40 ms", F_SMALL, MUTED)
    for i, h in enumerate(["192.168.1.65", "192.168.1.78"]):
        y = 200 + i * 52
        c.rect(cx, y, cw, 44, INK if i == 0 else RAISED, r=10)
        c.text(cx + 16, y + 22, h, F_LABEL, BG if i == 0 else INK, anchor="lm")
    c.rect(cx, 476, cw // 2 - 6, 44, INK, r=10)
    c.centred(cx, 476, cw // 2 - 6, 44, "Buscar QLC+", F_TILE_S, BG)
    c.rect(cx + cw // 2 + 6, 476, cw // 2 - 6, 44, RAISED, r=10)
    c.centred(cx + cw // 2 + 6, 476, cw // 2 - 6, 44, "Escribir IP", F_TILE_S, INK)
    # Footer
    c.d.line([(x0 + 24) * S, 536 * S, (x0 + 828 - 24) * S, 536 * S], fill=LINE, width=S)
    c.text(x0 + 24, 552, "BRILLO", F_SECTION, MUTED)
    c.rect(x0 + 96, 548, 420, 24, RAISED, r=12)
    c.rect(x0 + 96, 548, 300, 24, MUTED, r=12)
    c.text(x0 + 528, 560, "72%", F_SMALL, INK, anchor="lm")
    c.text(x0 + 600, 552, "ATENUAR CON BATERÍA", F_SECTION, MUTED)
    c.rect(x0 + 776, 548, 52, 24, RAISED, r=12)
    c.circle(x0 + 788, 560, 9, INK)
    c.save(path)


if __name__ == "__main__":
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    out.mkdir(parents=True, exist_ok=True)
    page_show(out / "mock-show.png")
    page_color(out / "mock-color.png")
    page_setup(out / "mock-setup.png")
    print("wrote", out)
