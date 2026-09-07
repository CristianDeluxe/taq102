#!/usr/bin/env python3
"""Turn the vendor GSL3673_FW array into a firmware blob for mainline silead.c.

    ./taq102-gsl3673-firmware.py gsl3673.h > gsl3673.fw
    install -D -m 0644 gsl3673.fw <rootfs>/lib/firmware/silead/gsl3673.fw

Why this is a pure repack and not a translation
-----------------------------------------------
The vendor header (drivers/input/touchscreen/gsl3673.h in the Rockchip 4.4
tree) stores the firmware as

    struct fw_data { u32 offset : 8; u32 : 0; u32 val; };

The anonymous zero-width bitfield closes the current storage unit, so on the
ARM EABI the record is eight bytes: the register offset in the low byte of
word 0, the value in word 1.  Verified by compiling that declaration with
arm-buildroot-linux-gnueabihf-gcc: sizeof == 8, offsetof(val) == 4.

Mainline silead.c reads

    struct silead_fw_data { u32 offset; u32 val; };

and walks the file with fw->size / 8 iterations of
i2c_smbus_write_i2c_block_data(offset, 4, &val).  The two structures are
identical, so the blob is the array itself, little-endian, in source order.
The register traffic is what the vendor's gsl_load_fw() emits; only the
batching differs (silead writes one word per transfer, the vendor packs up to
32 words behind one register address), and batching is invisible to the chip.

What this script deliberately does NOT convert
----------------------------------------------
gsl_config_data_id_3673[], the other big array in the same header, is not
firmware and has no image in a silead blob.  It is never written to i2c: its
one non-debug consumer is startup_chip() -> gsl_DataInit(), inside the
closed-source host-side algorithm in gsl_point_id.c.  silead.c has no such
layer, so there is nothing to carry across.

Which header to feed it
-----------------------
The header AFTER kernel/patches/0005-*, i.e. the one carrying the tablet's own
stock Android arrays (4719 records, 37752 bytes).  The array the vendor tree
ships untouched is another panel's and is the one that never raised an
interrupt for a finger.  The script prints the record count and byte size on
stderr; 4719 / 37752 is the expected pair for this board.

The blob is Silead's, never redistributable in linux-firmware, and belongs in
the appliance rootfs only.
"""
import re
import struct
import sys

RECORD = re.compile(r"\{\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\}")


def convert(header_text: str) -> bytes:
    start = header_text.index("GSL3673_FW[] = {")
    body = header_text[start:]
    body = body[: body.index("\n};")]

    out = bytearray()
    for offset_s, val_s in RECORD.findall(body):
        offset = int(offset_s, 16)
        val = int(val_s, 16)
        if offset > 0xFF:
            raise SystemExit(
                f"offset {offset:#x} does not fit in an i2c register address; "
                "this header is not in the layout this script understands"
            )
        out += struct.pack("<II", offset, val)
    if not out:
        raise SystemExit("no records found in GSL3673_FW[]")
    return bytes(out)


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write(f"usage: {sys.argv[0]} gsl3673.h > gsl3673.fw\n")
        return 2

    with open(sys.argv[1], encoding="latin-1") as fh:
        blob = convert(fh.read())

    sys.stderr.write(f"{len(blob) // 8} records, {len(blob)} bytes\n")
    sys.stdout.buffer.write(blob)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
