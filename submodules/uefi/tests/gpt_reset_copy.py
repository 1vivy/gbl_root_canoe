#!/usr/bin/env python3
"""Offline model of the minimal mutation placed ahead of the shipped GPT flush."""
import argparse
import binascii
import struct
import sys
from pathlib import Path

SECTOR = 512
ACTIVE = 1 << 50
RETRY_MASK = 7 << 51
UNBOOTABLE = 1 << 55

class ResetError(Exception):
    pass

def table(image, header_lba):
    h = header_lba * SECTOR
    if image[h:h + 8] != b"EFI PART":
        raise ResetError("missing GPT header")
    entries_lba, count, size = struct.unpack_from("<QII", image, h + 72)
    if count == 0 or size < 128 or count * size > len(image):
        raise ResetError("invalid GPT entry geometry")
    rows = {}
    base = entries_lba * SECTOR
    for i in range(count):
        off = base + i * size
        row = image[off:off + size]
        if row[:16] == bytes(16):
            continue
        name = row[56:128].decode("utf-16-le").split("\0", 1)[0].rstrip(" ")
        if name in rows:
            raise ResetError(f"duplicate partition {name}")
        rows[name] = off
    return h, base, count, size, rows

def update_crc(image, header_off, entries_off, count, size):
    entries_crc = binascii.crc32(image[entries_off:entries_off + count * size]) & 0xffffffff
    struct.pack_into("<I", image, header_off + 88, entries_crc)
    header_size = struct.unpack_from("<I", image, header_off + 12)[0]
    struct.pack_into("<I", image, header_off + 16, 0)
    header_crc = binascii.crc32(image[header_off:header_off + header_size]) & 0xffffffff
    struct.pack_into("<I", image, header_off + 16, header_crc)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("--retry", type=int, default=7)
    args = parser.parse_args()
    try:
        if not 0 <= args.retry <= 7:
            raise ResetError(f"retry value {args.retry} exceeds 3-bit field")
        image = bytearray(Path(args.source).read_bytes())
        tables = [table(image, 1), table(image, len(image) // SECTOR - 1)]
        attrs = {}
        for name in ("abl_a", "abl_b"):
            if any(name not in item[4] for item in tables):
                raise ResetError(f"target entry {name} missing")
            values = [struct.unpack_from("<Q", image, item[4][name] + 48)[0] for item in tables]
            if values[0] != values[1]:
                raise ResetError(f"{name} attributes differ between GPT copies")
            if values[0] == (1 << 64) - 1:
                raise ResetError(f"{name} has malformed all-ones attributes")
            attrs[name] = values[0]
        active = [name for name in ("abl_a", "abl_b") if attrs[name] & ACTIVE]
        if len(active) != 1:
            raise ResetError("GPT must mark exactly one abl slot active")
        target = active[0]
        new_attr = (attrs[target] & ~RETRY_MASK & ~UNBOOTABLE) | (args.retry << 51)
        for h, base, count, size, rows in tables:
            struct.pack_into("<Q", image, rows[target] + 48, new_attr)
            update_crc(image, h, base, count, size)
        Path(args.destination).write_bytes(image)
        print(f"RESET active={target} attributes={attrs[target]:016x}->{new_attr:016x}")
    except (ResetError, OSError, UnicodeDecodeError) as exc:
        print(f"REFUSED: {exc}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
