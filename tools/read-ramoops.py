#!/usr/bin/env python3
"""Decode a raw ramoops region dumped off the tablet with /dev/mem.

    dd if=/dev/mem bs=4096 skip=$((0x68100000/4096)) count=240 > ramoops.bin
    tools/read-ramoops.py ramoops.bin

Why a script rather than mounting /sys/fs/pstore: the region belongs to the
mainline kernel that crashed, and it is read back from the *vendor* kernel,
whose own pstore is a different implementation at a different address. So the
bytes have to be parsed here.

The layout is ramoops's own, from fs/pstore/ram_core.c: each zone starts with
struct persistent_ram_buffer { u32 sig; atomic_t start; atomic_t size; u8
data[]; }, sig == 0x43474244 ('DBGC'), and the data is a circular buffer whose
head is `start` once it has wrapped. Zone order and sizes follow the device
tree: record-size 0x20000 for the oops/panic records, console-size 0x80000,
pmsg-size 0x40000 (mainline rounds a non-power-of-two down, so the DT's
0x50000 really is 0x40000).
"""
import sys, struct

SIG = 0x43474244  # 'DBGC'


def zone(buf, off, size, name):
    """Return the readable contents of one ramoops zone, oldest byte first."""
    if off + 12 > len(buf):
        return None
    sig, start, used = struct.unpack_from('<III', buf, off)
    if sig != SIG:
        return None
    data = buf[off + 12:off + size]
    if used > len(data):
        used = len(data)
    # Once the buffer has wrapped, `used` is the whole capacity and `start` is
    # the head; before that the bytes are simply the first `used`.
    if used == len(data) and start:
        text = data[start:] + data[:start]
    else:
        text = data[:used]
    return name, start, used, text


def main(path):
    buf = open(path, 'rb').read()
    print(f"{path}: {len(buf)} bytes\n")

    # Zone layout from the device tree, in the order ramoops allocates them.
    layout = [(0x80000, 'console'), (0x40000, 'pmsg')]
    records = 0x20000

    found = []
    off = 0
    # The dump may not start exactly at the region base, so scan for signatures
    # on 4 KB boundaries as well as taking the declared layout at face value.
    while off + 12 <= len(buf):
        sig, start, used = struct.unpack_from('<III', buf, off)
        if sig == SIG and used <= len(buf) - off:
            found.append((off, start, used))
        off += 0x1000

    if not found:
        nonzero = sum(1 for b in buf if b)
        print("No ramoops signature found.")
        print(f"{nonzero} non-zero bytes of {len(buf)}.")
        if nonzero == 0:
            print("The region is entirely zero: the memory was cleared, or the "
                  "kernel never registered ramoops.")
        else:
            print("Something is there but it is not a ramoops zone. Raw strings:")
            show_strings(buf)
        return 1

    for off, start, used in found:
        size = next((s for s, _ in layout if off + s <= len(buf)), records)
        r = zone(buf, off, size, f"zone@+0x{off:x}")
        if not r:
            continue
        name, start, used, text = r
        # A zone that has never wrapped reports its whole capacity as `used`,
        # so the tail is padding. Printing a quarter of a megabyte of NULs
        # buries the log that is actually there.
        text = text.rstrip(b'\0')
        # A wrapped zone can carry NUL runs where the log has holes; collapse
        # them so a real message is not buried in padding.
        import re as _re
        text = _re.sub(rb'\x00{16,}', b'\n[... NUL padding ...]\n', text)
        if not text:
            print(f"=== {name}: empty (signature present, no content)\n")
            continue
        print(f"=== {name}: {len(text)} bytes of content "
              f"(start={start}, declared used={used})")
        print(text.decode('utf-8', errors='replace').rstrip())
        print()
    return 0


def show_strings(buf, minlen=12):
    run = bytearray()
    shown = 0
    for b in buf:
        if 32 <= b < 127 or b in (10, 13, 9):
            run.append(b)
        else:
            if len(run) >= minlen:
                print('  ' + run.decode('ascii', 'replace').strip())
                shown += 1
                if shown > 200:
                    print('  ... truncated')
                    return
            run = bytearray()


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
