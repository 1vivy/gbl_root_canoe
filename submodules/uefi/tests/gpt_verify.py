#!/usr/bin/env python3
"""Independent GPT validator and semantic diff checker for the L2 fixture."""
import argparse
import binascii
import struct
import sys
from pathlib import Path

SECTOR = 512
ATTR_OFFSET = 48

class GptError(Exception):
    pass

def u32(data, off): return struct.unpack_from("<I", data, off)[0]
def u64(data, off): return struct.unpack_from("<Q", data, off)[0]

def parse_copy(image, header_lba, label):
    off = header_lba * SECTOR
    header = bytearray(image[off:off + SECTOR])
    if len(header) != SECTOR or header[:8] != b"EFI PART":
        raise GptError(f"{label}: missing GPT header")
    size = u32(header, 12)
    if size < 92 or size > SECTOR:
        raise GptError(f"{label}: invalid header size {size}")
    expected = u32(header, 16)
    struct.pack_into("<I", header, 16, 0)
    actual = binascii.crc32(header[:size]) & 0xffffffff
    if actual != expected:
        raise GptError(f"{label}: header CRC invalid expected={expected:08x} actual={actual:08x}")
    entries_lba, count, entry_size, entries_crc = struct.unpack_from("<QIII", header, 72)
    if count == 0 or entry_size < 128 or count * entry_size > len(image):
        raise GptError(f"{label}: invalid entry geometry")
    entries_off = entries_lba * SECTOR
    entries = image[entries_off:entries_off + count * entry_size]
    actual_entries_crc = binascii.crc32(entries) & 0xffffffff
    if actual_entries_crc != entries_crc:
        raise GptError(f"{label}: entry-array CRC invalid expected={entries_crc:08x} actual={actual_entries_crc:08x}")
    rows = {}
    for index in range(count):
        row_off = entries_off + index * entry_size
        row = image[row_off:row_off + entry_size]
        if row[:16] == bytes(16):
            continue
        try:
            name = row[56:128].decode("utf-16-le").split("\0", 1)[0].rstrip(" ")
        except UnicodeDecodeError as exc:
            raise GptError(f"{label}: malformed partition name") from exc
        if name in rows:
            raise GptError(f"{label}: duplicate partition {name}")
        rows[name] = (index, row_off, u64(row, ATTR_OFFSET))
    return {"label": label, "header_off": off, "header_size": size,
            "entries_off": entries_off, "entries_size": count * entry_size,
            "entry_size": entry_size, "rows": rows}

def parse_image(path):
    image = Path(path).read_bytes()
    if len(image) < 3 * SECTOR or len(image) % SECTOR:
        raise GptError("image size is not whole sectors")
    primary = parse_copy(image, 1, "PRIMARY")
    backup = parse_copy(image, len(image) // SECTOR - 1, "BACKUP")
    for name in set(primary["rows"]) | set(backup["rows"]):
        if name not in primary["rows"] or name not in backup["rows"]:
            raise GptError(f"partition {name} missing from one GPT copy")
        if primary["rows"][name][2] != backup["rows"][name][2]:
            raise GptError(f"partition {name} attributes differ between GPT copies")
    return image, primary, backup

def compare(before_path, after_path, target):
    before, bp, bb = parse_image(before_path)
    after, ap, ab = parse_image(after_path)
    if len(before) != len(after):
        raise GptError("image size changed")
    for table in (bp, bb, ap, ab):
        if target not in table["rows"]:
            raise GptError(f"target partition {target} missing")
    before_attr = bp["rows"][target][2]
    after_attr = ap["rows"][target][2]
    expected = (before_attr & ~(((1 << 3) - 1) << 51) & ~(1 << 55)) | (7 << 51)
    if after_attr != expected:
        raise GptError(f"target attributes wrong before={before_attr:016x} after={after_attr:016x} expected={expected:016x}")
    for name, (_, _, value) in bp["rows"].items():
        if name != target and ap["rows"].get(name, (None, None, None))[2] != value:
            raise GptError(f"unintended attribute change on {name}")
    allowed = set()
    for old_table, new_table in ((bp, ap), (bb, ab)):
        old_row = old_table["rows"][target][1]
        new_row = new_table["rows"][target][1]
        if old_row != new_row:
            raise GptError("partition row moved")
        allowed.update(range(old_row + ATTR_OFFSET, old_row + ATTR_OFFSET + 8))
        allowed.update(range(old_table["header_off"] + 16, old_table["header_off"] + 20))
        allowed.update(range(old_table["header_off"] + 88, old_table["header_off"] + 92))
    changed = {i for i, pair in enumerate(zip(before, after)) if pair[0] != pair[1]}
    unexpected = changed - allowed
    if unexpected:
        raise GptError(f"unexpected changed byte offsets: {sorted(unexpected)[:16]}")
    semantic = changed & {
        bp["rows"][target][1] + ATTR_OFFSET + i for i in range(8)
    } | changed & {
        bb["rows"][target][1] + ATTR_OFFSET + i for i in range(8)
    }
    if not semantic:
        raise GptError("no target attribute byte changed")
    print(f"DIFF exactly intended field changed: {target} attributes {before_attr:016x} -> {after_attr:016x}; CRC metadata only otherwise")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("--compare")
    parser.add_argument("--target", default="abl_a")
    args = parser.parse_args()
    try:
        _, primary, backup = parse_image(args.image)
        print("PRIMARY header CRC valid; PRIMARY entry-array CRC valid")
        print("BACKUP header CRC valid; BACKUP entry-array CRC valid")
        if args.compare:
            compare(args.compare, args.image, args.target)
    except (GptError, OSError) as exc:
        print(f"REJECT: {exc}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
