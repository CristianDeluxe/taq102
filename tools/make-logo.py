#!/usr/bin/env python3
"""Build the boot logo U-Boot draws, from a transparent PNG on black.

    make-logo.py <logo.png> <out.bmp> [width] [height]

The stock image this replaces is 1024x600, 8 bits per pixel with a palette,
and RLE8 compressed, so this writes the same: Rockchip's U-Boot reads that
form, and a two-colour picture compresses to a few kilobytes under RLE8 where
an uncompressed 8-bit frame is 600 KB.

Pillow cannot write RLE8, so the encoder is here. BMP rows run bottom-up.
"""
import sys
from PIL import Image

BLACK, WHITE = 0, 255


def compose(png_path, width, height, margin=0.62):
    """The logo centred on black, scaled to `margin` of the panel's width."""
    logo = Image.open(png_path).convert("RGBA")
    target_w = int(width * margin)
    scale = target_w / logo.width
    target_h = int(logo.height * scale)
    if target_h > height * margin:
        scale = (height * margin) / logo.height
        target_w, target_h = int(logo.width * scale), int(logo.height * scale)
    logo = logo.resize((target_w, target_h), Image.LANCZOS)

    canvas = Image.new("L", (width, height), 0)
    # The alpha channel is the shape; the artwork itself is already white.
    mask = logo.getchannel("A").point(lambda a: 255 if a > 127 else 0)
    canvas.paste(255, ((width - target_w) // 2, (height - target_h) // 2), mask)
    return canvas.point(lambda v: WHITE if v > 127 else BLACK)


def rle8(image, width, height):
    """Encode bottom-up RLE8: runs of up to 255, then the end-of-line marker."""
    out = bytearray()
    pixels = image.load()
    for y in range(height - 1, -1, -1):
        x = 0
        while x < width:
            value = pixels[x, y]
            run = 1
            while x + run < width and pixels[x + run, y] == value and run < 255:
                run += 1
            out += bytes((run, value))
            x += run
        out += b"\x00\x00"          # end of line
    out += b"\x00\x01"              # end of bitmap
    return bytes(out)


def write_bmp(path, payload, width, height, palette):
    header_size = 14 + 40 + len(palette) * 4
    table = bytearray()
    for r, g, b in palette:
        table += bytes((b, g, r, 0))
    out = bytearray()
    out += b"BM"
    out += (header_size + len(payload)).to_bytes(4, "little")
    out += (0).to_bytes(4, "little")
    out += header_size.to_bytes(4, "little")
    out += (40).to_bytes(4, "little")
    out += width.to_bytes(4, "little", signed=True)
    out += height.to_bytes(4, "little", signed=True)
    out += (1).to_bytes(2, "little")        # planes
    out += (8).to_bytes(2, "little")        # bits per pixel
    out += (1).to_bytes(4, "little")        # BI_RLE8
    out += len(payload).to_bytes(4, "little")
    out += (2835).to_bytes(4, "little", signed=True)
    out += (2835).to_bytes(4, "little", signed=True)
    out += len(palette).to_bytes(4, "little")
    out += len(palette).to_bytes(4, "little")
    out += table
    out += payload
    open(path, "wb").write(bytes(out))
    return len(out)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    png, out = sys.argv[1], sys.argv[2]
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 1024
    height = int(sys.argv[4]) if len(sys.argv) > 4 else 600
    image = compose(png, width, height)
    # A full 256-entry table, like the image this replaces. A two-entry palette
    # is legal and a third of a kilobyte smaller, but readers are entitled to
    # treat 8 bits per pixel as 256 colours and at least one decodes a short
    # table as a 1-bit image instead.
    palette = [(0, 0, 0)] * 256
    palette[WHITE] = (255, 255, 255)
    size = write_bmp(out, rle8(image, width, height), width, height, palette)
    print(f"{out}: {width}x{height}, 8 bpp, RLE8, {size} bytes")


if __name__ == "__main__":
    main()
