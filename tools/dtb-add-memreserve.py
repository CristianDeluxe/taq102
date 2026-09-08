#!/usr/bin/env python3
"""Add a /memreserve/ entry to a flattened device tree, without recompiling it.

    dtb-add-memreserve.py in.dtb out.dtb 0x68100000 0xf0000

Why not just decompile, edit and recompile: the vendor DTB on this board was
built by the vendor's own toolchain, and a round trip through a different dtc
produced an image that would not boot -- v57, 2026-09-08. This edits the
binary instead, so every byte of the struct and strings blocks is preserved
exactly as shipped.

The format (v17) is a header, a memory reserve map of 64-bit (address, size)
pairs terminated by a zero pair, then the struct and strings blocks. Adding an
entry means growing the reserve map by 16 bytes and moving the two blocks that
follow, which is only three offsets in the header.
"""
import struct
import sys


def main(src, dst, addr, size):
    d = open(src, 'rb').read()
    (magic, total, off_struct, off_strings, off_rsvmap, ver,
     last_comp, boot_cpu, size_strings, size_struct) = struct.unpack('>10I', d[:40])
    if magic != 0xd00dfeed:
        raise SystemExit(f"not a dtb: magic {magic:#x}")
    if ver < 17:
        raise SystemExit(f"version {ver} too old for this script")

    # Read the existing reserve map, so an entry is appended rather than lost.
    entries = []
    p = off_rsvmap
    while True:
        a, s = struct.unpack_from('>QQ', d, p)
        p += 16
        if a == 0 and s == 0:
            break
        entries.append((a, s))
    for a, s in entries:
        if addr < a + s and a < addr + size:
            raise SystemExit(f"overlaps existing reserve {a:#x}+{s:#x}")
    entries.append((addr, size))

    rsvmap = b''.join(struct.pack('>QQ', a, s) for a, s in entries)
    rsvmap += struct.pack('>QQ', 0, 0)

    new_off_struct = off_rsvmap + len(rsvmap)
    shift = new_off_struct - off_struct
    if shift % 8:
        raise SystemExit("reserve map must stay 8-byte aligned")
    new_off_strings = off_strings + shift
    new_total = total + shift

    head = bytearray(d[:off_rsvmap])
    struct.pack_into('>I', head, 4, new_total)
    struct.pack_into('>I', head, 8, new_off_struct)
    struct.pack_into('>I', head, 12, new_off_strings)

    out = (bytes(head) + rsvmap +
           d[off_struct:off_struct + size_struct] +
           d[off_strings:off_strings + size_strings])
    if len(out) != new_total:
        raise SystemExit(f"size mismatch: built {len(out)}, header says {new_total}")
    open(dst, 'wb').write(out)

    print(f"{src} -> {dst}")
    print(f"  reserve map: {len(entries)} entr{'y' if len(entries)==1 else 'ies'}, "
          f"added {addr:#x}+{size:#x}")
    print(f"  struct and strings moved {shift} bytes, total {total} -> {new_total}")
    print(f"  struct block byte-identical: "
          f"{out[new_off_struct:new_off_struct+size_struct] == d[off_struct:off_struct+size_struct]}")


if __name__ == '__main__':
    if len(sys.argv) != 5:
        print(__doc__)
        raise SystemExit(2)
    main(sys.argv[1], sys.argv[2], int(sys.argv[3], 0), int(sys.argv[4], 0))
