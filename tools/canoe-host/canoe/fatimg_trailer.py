"""Canonical canoe trailer encoding and verification."""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from typing import Final, Sequence

from .errors import CanoeError

TRAILER: Final = 4096
SECTOR: Final = 512
MAGIC: Final = b"CANOEFT1"

@dataclass(frozen=True, slots=True)
class Verification:
    """Trailer verification outcome and human-readable mismatches."""
    ok: bool
    mismatches: tuple[str, ...]

def runs_bytes(runs: Sequence[tuple[int, int]]) -> bytes:
    """Encode physical runs exactly as the device-side contract specifies."""
    packed = bytearray()
    for physical, count in runs:
        if physical < 0 or count <= 0:
            raise CanoeError("extent runs require a non-negative block and positive count")
        packed += struct.pack("<QQ", physical, count)
    return bytes(packed)

def trailer_bytes(file_size: int, runs: Sequence[tuple[int, int]]) -> bytes:
    """Return the canonical 4096-byte canoe trailer."""
    if file_size < TRAILER or file_size % SECTOR:
        raise CanoeError("file size must be sector-aligned and include a trailer")
    trailer = bytearray(TRAILER)
    trailer[:8] = MAGIC
    struct.pack_into("<QQII", trailer, 8, file_size, file_size - TRAILER, len(runs), 0)
    trailer[0x20:0x40] = hashlib.sha256(runs_bytes(runs)).digest()
    return bytes(trailer)

def parse_runs(values: Sequence[str]) -> tuple[tuple[int, int], ...]:
    """Parse physical_block:block_count arguments at the CLI boundary."""
    runs: list[tuple[int, int]] = []
    for value in values:
        try:
            physical, count = value.split(":", 1)
            runs.append((int(physical, 0), int(count, 0)))
        except (ValueError, TypeError) as exc:
            raise CanoeError(f"invalid extent run {value!r}; use physical_block:block_count") from exc
    runs_bytes(runs)
    return tuple(runs)

def verify_bytes(raw: bytes, expected_runs: Sequence[tuple[int, int]]) -> Verification:
    """Compare trailer fields against file geometry and supplied runs."""
    mismatches: list[str] = []
    if len(raw) < TRAILER:
        return Verification(False, ("trailer length",))
    trailer = raw[-TRAILER:]
    if trailer[:8] != MAGIC:
        mismatches.append("magic")
    file_size, volume, count, reserved = struct.unpack_from("<QQII", trailer, 8)
    if file_size != len(raw):
        mismatches.append("file size")
    if volume != len(raw) - TRAILER:
        mismatches.append("volume size")
    if count != len(expected_runs):
        mismatches.append("run count")
    if reserved != 0:
        mismatches.append("reserved")
    if trailer[0x20:0x40] != hashlib.sha256(runs_bytes(expected_runs)).digest():
        mismatches.append("run-list hash")
    return Verification(not mismatches, tuple(mismatches))
