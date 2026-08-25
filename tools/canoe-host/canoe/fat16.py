"""Pure-Python FAT16 volume writer and directory reader."""
# noqa: SIZE_OK — FAT16 writer and reader share one on-disk geometry implementation.

from __future__ import annotations

import struct
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Final

from .errors import CanoeError

SECTOR: Final = 512
TRAILER: Final = 4096
EOC: Final = 0xFFFF

def _find_mtools() -> str | None:
    """Detect optional mtools while keeping the deterministic writer pure Python."""
    return shutil.which("mformat")


@dataclass(slots=True)  # noqa: MUTABLE_OK
class _Node:
    """Mutable tree node used while laying out directory entries."""

    name: str
    data: bytes | None = None
    children: list[_Node] = field(default_factory=list)
    short: str = ""
    cluster: int = 0


def _short_name(name: str, used: set[str]) -> str:
    """Create a legal unique 8.3 alias for an entry."""
    base, dot, ext = name.rpartition(".")
    if not dot:
        base, ext = name, ""
    clean = "".join(ch for ch in base.upper() if ch.isalnum() or ch in "_$~!") or "FILE"
    suffix = "".join(ch for ch in ext.upper() if ch.isalnum() or ch in "_$~!")
    candidate = f"{clean[:8]}.{suffix[:3]}" if suffix else clean[:8]
    if len(clean) <= 8 and len(suffix) <= 3 and candidate not in used:
        used.add(candidate)
        return candidate
    stem = clean[:6]
    index = 1
    while True:
        candidate = f"{stem[: max(1, 8 - len(str(index)) - 1)]}~{index}"
        if suffix:
            candidate += f".{suffix[:3]}"
        if candidate not in used:
            used.add(candidate)
            return candidate
        index += 1


def _lfn_entries(name: str, short: str) -> list[bytes]:
    """Encode a long filename as VFAT directory entries, in on-disk order."""
    encoded = name.encode("utf-16le") + b"\0\0"
    units = [encoded[index : index + 2] for index in range(0, len(encoded), 2)]
    chunks = [units[index : index + 13] for index in range(0, len(units), 13)]
    raw_short = short.replace(".", "").ljust(11)[:11].encode("ascii")
    checksum = 0
    for byte in raw_short:
        checksum = ((checksum >> 1) | ((checksum & 1) << 7)) + byte
        checksum &= 0xFF
    entries: list[bytes] = []
    for index, chunk in reversed(list(enumerate(chunks, 1))):
        entry = bytearray(32)
        entry[0] = index | (0x40 if index == len(chunks) else 0)
        entry[11] = 0x0F
        entry[13] = checksum
        chars = chunk + [b"\xff\xff"] * (13 - len(chunk))
        if len(chunk) < 13:
            chars[len(chunk)] = b"\0\0"
        for offset, unit in zip((1, 3, 5, 7, 9), chars[:5]):
            entry[offset : offset + 2] = unit
        for offset, unit in zip((14, 16, 18, 20, 22, 24), chars[5:11]):
            entry[offset : offset + 2] = unit
        for offset, unit in zip((28, 30), chars[11:13]):
            entry[offset : offset + 2] = unit
        entries.append(bytes(entry))
    return entries


def _entry(node: _Node, directory: bool = False) -> list[bytes]:
    """Encode one node's LFN and short entries."""
    result = _lfn_entries(node.name, node.short) if node.name != node.short else []
    entry = bytearray(32)
    parts = node.short.split(".", 1)
    raw_name = parts[0].ljust(8) + (parts[1].ljust(3) if len(parts) == 2 else "   ")
    entry[:11] = raw_name[:11].encode("ascii")
    entry[11] = 0x10 if directory else 0x20
    struct.pack_into("<H", entry, 26, node.cluster)
    struct.pack_into("<I", entry, 28, 0 if directory else len(node.data or b""))
    result.append(bytes(entry))
    return result


def _tree(staging: Path) -> _Node:
    """Read a staging tree into a deterministic directory tree."""
    if not staging.is_dir():
        raise CanoeError(f"staging tree is missing: {staging}")
    root = _Node("")
    nodes: dict[Path, _Node] = {staging: root}
    for path in sorted(staging.rglob("*")):
        parent = nodes[path.parent]
        node = _Node(path.name, None if path.is_dir() else path.read_bytes())
        used = {child.short for child in parent.children}
        node.short = _short_name(node.name, used)
        parent.children.append(node)
        if path.is_dir():
            nodes[path] = node
    return root


@dataclass(frozen=True, slots=True)
class BuildResult:
    """Summary of a completed FAT volume build."""

    files: int
    bytes_written: int
    filesystem_path: str


def build_fat16(staging: Path, output: Path, target_size: int, trailer_factory) -> BuildResult:
    """Build a FAT16 volume and append a trailer supplied by the orchestrator."""
    root = _tree(staging)
    files = [node for node in _walk(root) if node.data is not None]
    content = sum(len(node.data or b"") for node in files)
    if target_size < content + 1024 * 1024 + TRAILER + 128 * SECTOR:
        raise CanoeError(f"content does not fit requested image size ({target_size} bytes)")
    total = max(target_size, 3 * 1024 * 1024)
    total = ((total + SECTOR - 1) // SECTOR) * SECTOR
    volume = total - TRAILER
    spc, fat_sectors, root_sectors, cluster_count = _geometry(volume, len(files) + len(_dirs(root)))
    image = bytearray(volume)
    first_data = 1 + 2 * fat_sectors + root_sectors
    next_cluster = 2
    for directory in _dirs(root):
        directory.cluster = next_cluster
        next_cluster += spc
    for node in files:
        count = max(1, (len(node.data or b"") + spc * SECTOR - 1) // (spc * SECTOR))
        node.cluster = next_cluster
        next_cluster += count * spc
    if next_cluster - 2 > cluster_count:
        raise CanoeError("content does not fit FAT16 data area")
    _write_bpb(image, volume, spc, fat_sectors, root_sectors)
    fat = bytearray(fat_sectors * SECTOR)
    struct.pack_into("<HH", fat, 0, 0xFFF8, EOC)
    for directory in _dirs(root):
        _chain(fat, directory.cluster, spc)
    for node in files:
        count = max(1, (len(node.data or b"") + spc * SECTOR - 1) // (spc * SECTOR))
        _chain(fat, node.cluster, spc, count)
        _write_data(image, first_data, spc, node.cluster, node.data or b"")
    image[SECTOR : SECTOR + len(fat)] = fat
    image[SECTOR + fat_sectors * SECTOR : SECTOR + 2 * fat_sectors * SECTOR] = fat
    root_offset = 1 + 2 * fat_sectors
    _find_mtools()
    _write_directory(image, first_data, spc, root_sectors, root, False, root_offset)
    for directory in _dirs(root):
        _write_directory(image, first_data, spc, root_sectors, directory, True, root_offset)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bytes(image) + trailer_factory(total, ()))
    return BuildResult(len(files), total, "pure-python")
def _geometry(volume: int, files: int) -> tuple[int, int, int, int]:
    """Return sectors-per-cluster, FAT sectors, root sectors and cluster count."""
    if volume % SECTOR:
        raise CanoeError("FAT volume must be sector-aligned")
    root_sectors = max(32, ((files * 32 + SECTOR - 1) // SECTOR) * 2)
    sectors = volume // SECTOR
    for sectors_per_cluster in (1, 2, 4, 8, 16, 32):
        fat_sectors = 1
        clusters = 0
        for _ in range(8):
            clusters = (sectors - 1 - 2 * fat_sectors - root_sectors) // sectors_per_cluster
            wanted = ((clusters + 2) * 2 + SECTOR - 1) // SECTOR
            if wanted == fat_sectors:
                break
            fat_sectors = wanted
        if 4085 <= clusters <= 65524:
            return sectors_per_cluster, fat_sectors, root_sectors, clusters
    raise CanoeError("requested image cannot be represented as FAT16")


def _walk(node: _Node) -> list[_Node]:
    result: list[_Node] = []
    for child in node.children:
        result.extend(_walk(child) if child.data is None else [child])
    return result


def _dirs(node: _Node) -> list[_Node]:
    return [child for child in node.children if child.data is None] + [grand for child in node.children if child.data is None for grand in _dirs(child)]

def _write_bpb(image: bytearray, volume: int, spc: int, fat: int, root: int) -> None:
    """Write a FAT16 BIOS parameter block."""
    image[0:3] = b"\xEB\x3C\x90"
    image[3:11] = b"CANOEFAT"
    struct.pack_into("<HBHBHHBHHHII", image, 11, SECTOR, spc, 1, 2, root * SECTOR // 32, 0, 0xF8, fat, 0, 0, 0, 0)
    struct.pack_into("<I", image, 32, volume // SECTOR)
    struct.pack_into("<B", image, 38, 0x29)
    image[43:54] = b"CANOEFAT   "
    image[54:62] = b"FAT16   "
    image[510:512] = b"\x55\xaa"


def _chain(fat: bytearray, start: int, spc: int, count: int = 1) -> None:
    """Set a contiguous cluster chain in a FAT."""
    for index in range(count):
        cluster = start + index * spc
        for offset in range(spc):
            current = cluster + offset
            following = current + 1 if offset + 1 < spc or index + 1 < count else EOC
            struct.pack_into("<H", fat, current * 2, following)

def _write_data(image: bytearray, first_data: int, spc: int, start: int, data: bytes) -> None:
    offset = (first_data + (start - 2) * spc) * SECTOR
    image[offset : offset + len(data)] = data


def _write_directory(image: bytearray, first_data: int, spc: int, root_sectors: int, node: _Node, nested: bool, root_offset: int) -> None:
    """Write root or one-cluster nested directory."""
    entries: list[bytes] = []
    if nested:
        dot = _Node(".", b""); dot.short = "."; dot.cluster = node.cluster
        parent = _Node("..", b""); parent.short = ".."; parent.cluster = 0
        entries.extend(_entry(dot, True)); entries.extend(_entry(parent, True))
    for child in node.children:
        entries.extend(_entry(child, child.data is None))
    payload = b"".join(entries) + b"\0" * 32
    if nested:
        offset = (first_data + (node.cluster - 2) * spc) * SECTOR
        limit = spc * SECTOR
    else:
        offset = root_offset * SECTOR
        limit = root_sectors * SECTOR
    if len(payload) > limit:
        raise CanoeError("directory has too many entries")
    image[offset : offset + len(payload)] = payload


@dataclass(frozen=True, slots=True)
class _File:
    path: str
    size: int
    cluster: int


def _read_volume(image: bytes) -> tuple[int, int, int, int, int]:
    """Read and validate the FAT16 geometry."""
    if image[510:512] != b"\x55\xaa":
        raise CanoeError("not a FAT volume")
    sector, spc, reserved, fats, root_entries, _ = struct.unpack_from("<HBHBHH", image, 11)
    fat_sectors = struct.unpack_from("<H", image, 22)[0]
    if sector != SECTOR or reserved != 1 or fats != 2 or fat_sectors == 0:
        raise CanoeError("unsupported FAT geometry")
    root_sectors = (root_entries * 32 + SECTOR - 1) // SECTOR
    first_data = reserved + fats * fat_sectors + root_sectors
    return spc, fat_sectors, root_sectors, first_data, root_entries


def _directory(image: bytes, geometry: tuple[int, int, int, int, int]) -> list[_File]:
    """Read FAT16 directories into paths and file extents."""
    spc, fat_sectors, root_sectors, first_data, _ = geometry
    fat = image[SECTOR : SECTOR + fat_sectors * SECTOR]
    result: list[_File] = []

    def read_dir(offset: int, size: int, prefix: str, seen: set[int]) -> None:
        long_name: list[str] = []
        for entry_offset in range(offset, offset + size, 32):
            entry = image[entry_offset : entry_offset + 32]
            if not entry or entry[0] == 0:
                break
            if entry[0] == 0xE5:
                long_name.clear(); continue
            if entry[11] == 0x0F:
                chars = entry[1:11] + entry[14:26] + entry[28:32]
                part = chars.decode("utf-16le", errors="ignore").rstrip("\0\uffff")
                long_name.insert(0, part)
                continue
            if entry[11] & 0x08:
                long_name.clear(); continue
            raw = entry[:11].decode("ascii", errors="replace").rstrip(" ")
            name = "".join(long_name) or (raw[:8] + ("." + raw[8:] if raw[8:] else ""))
            long_name.clear()
            cluster = struct.unpack_from("<H", entry, 26)[0]
            size_bytes = struct.unpack_from("<I", entry, 28)[0]
            if name in (".", ".."):
                continue
            path = f"{prefix}/{name}" if prefix else name
            if entry[11] & 0x10:
                if cluster in seen or cluster < 2:
                    continue
                chain = _clusters(fat, cluster)
                read_dir(first_data * SECTOR + (cluster - 2) * spc * SECTOR, len(chain) * spc * SECTOR, path, seen | {cluster})
            else:
                result.append(_File(path, size_bytes, cluster))

    read_dir((1 + 2 * fat_sectors) * SECTOR, root_sectors * SECTOR, "", set())
    return result


def _clusters(fat: bytes, start: int) -> list[int]:
    """Follow a FAT16 cluster chain."""
    result: list[int] = []
    cluster = start
    while 2 <= cluster < 0xFFF8 and cluster not in result:
        result.append(cluster)
        cluster = struct.unpack_from("<H", fat, cluster * 2)[0]
    return result


def _load_image(path: Path) -> tuple[bytes, bytes, tuple[int, int, int, int, int]]:
    """Load the volume and trailer from an image."""
    raw = path.read_bytes()
    if len(raw) < TRAILER:
        raise CanoeError("image is shorter than its trailer")
    trailer = raw[-TRAILER:]
    return raw[:-TRAILER], trailer, _read_volume(raw[:-TRAILER])


def list_files(image: Path) -> list[tuple[str, int]]:
    """List files in an image without mounting it."""
    volume, _, geometry = _load_image(image)
    return [(entry.path, entry.size) for entry in _directory(volume, geometry)]


def extract_files(image: Path, destination: Path) -> None:
    """Extract all files from an image into a directory tree."""
    volume, _, geometry = _load_image(image)
    destination.mkdir(parents=True, exist_ok=True)
    spc, _, _, first_data, _ = geometry
    fat = volume[SECTOR : SECTOR + geometry[1] * SECTOR]
    for entry in _directory(volume, geometry):
        data = bytearray()
        for cluster in _clusters(fat, entry.cluster):
            offset = (first_data + (cluster - 2) * spc) * SECTOR
            data.extend(volume[offset : offset + spc * SECTOR])
        target = destination / entry.path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(bytes(data[: entry.size]))


