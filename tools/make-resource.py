#!/usr/bin/env python3
"""Rebuild a Rockchip RSCE resource image, replacing rk-kernel.dtb.

The format is 512-byte blocks: one header block, then one index block per
entry, then the payloads in order. An index block is "ENTR", a NUL-padded
name, and at offset 0x100 a reserved word, the payload's start block, and its
size in bytes.

Both numbers in that index are what a hand-built image gets wrong. An image
carrying one entry starts its payload at block 2, not at the block 4 a
three-entry stock image uses, and the header's entry count has to shrink with
it. Copying a stock header into a one-entry image leaves U-Boot reading the FDT
1 KB into the blob: it finds no magic, boots nothing, and prints nothing --
which looks exactly like a kernel that does not boot.

Keeping the logo entries is not cosmetic either. U-Boot draws logo.bmp from
this image, so dropping them means the boot logo silently stops working.

Usage: make-resource.py <stock.rsce> <new-dtb> <out.rsce> [logo.bmp]

With a logo given, both `logo.bmp` and `logo_kernel.bmp` are replaced with it,
which is how the boot screen stops being the vendor's white one. U-Boot reads
8-bit RLE8 BMPs at the panel's own size; `make-logo.py` writes that form.
"""
import sys

BLOCK = 512
NAME_OFF = 4
INDEX_OFF = 0x100
DTB_NAME = "rk-kernel.dtb"
LOGO_NAMES = ("logo.bmp", "logo_kernel.bmp")


def read_entries(blob):
    """Return [(name, payload)] in index order."""
    if blob[:4] != b"RSCE":
        raise SystemExit("not a resource image")
    count = int.from_bytes(blob[0x0C:0x10], "little")
    entries = []
    for i in range(count):
        index = blob[BLOCK * (1 + i) : BLOCK * (2 + i)]
        if index[:4] != b"ENTR":
            raise SystemExit(f"entry {i} is not an ENTR block")
        name = index[NAME_OFF:INDEX_OFF].split(b"\0")[0].decode()
        start = int.from_bytes(index[INDEX_OFF + 4 : INDEX_OFF + 8], "little")
        size = int.from_bytes(index[INDEX_OFF + 8 : INDEX_OFF + 12], "little")
        entries.append((name, blob[BLOCK * start : BLOCK * start + size]))
    return entries


def write_image(header, entries):
    out = bytearray(header[:BLOCK])
    out[0x0C:0x10] = len(entries).to_bytes(4, "little")

    start = 1 + len(entries)
    payloads = bytearray()
    for name, payload in entries:
        index = bytearray(BLOCK)
        index[0:4] = b"ENTR"
        index[NAME_OFF : NAME_OFF + len(name)] = name.encode()
        index[INDEX_OFF + 4 : INDEX_OFF + 8] = start.to_bytes(4, "little")
        index[INDEX_OFF + 8 : INDEX_OFF + 12] = len(payload).to_bytes(4, "little")
        out += index

        blocks = (len(payload) + BLOCK - 1) // BLOCK
        payloads += payload.ljust(blocks * BLOCK, b"\0")
        start += blocks

    return bytes(out + payloads)


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    stock, dtb_path, out_path = sys.argv[1:4]
    logo_path = sys.argv[4] if len(sys.argv) > 4 else None
    blob = open(stock, "rb").read()
    dtb = open(dtb_path, "rb").read()
    if dtb[:4] != b"\xd0\x0d\xfe\xed":
        raise SystemExit("replacement is not a device tree blob")

    logo = None
    if logo_path:
        logo = open(logo_path, "rb").read()
        if logo[:2] != b"BM":
            raise SystemExit("replacement logo is not a BMP")

    entries = read_entries(blob)
    if not any(name == DTB_NAME for name, _ in entries):
        raise SystemExit(f"{stock} carries no {DTB_NAME}")

    def replacement(name, payload):
        if name == DTB_NAME:
            return dtb
        if logo and name in LOGO_NAMES:
            return logo
        return payload

    entries = [(name, replacement(name, p)) for name, p in entries]

    open(out_path, "wb").write(write_image(blob, entries))
    print(f"{out_path}: " + ", ".join(f"{n} {len(p)}" for n, p in entries))


if __name__ == "__main__":
    main()
