"""Behavioral coverage for the host-side efisp.fat trailer tools."""

from __future__ import annotations

import hashlib
import struct
from pathlib import Path

from canoe import fatimg


def test_trailer_layout_matches_contract(tmp_path: Path) -> None:
    image = tmp_path / "efisp.fat"
    image.write_bytes(b"FAT" * 4096)
    fatimg.rewrite_trailer(image, ((12, 3), (99, 7)))

    raw = image.read_bytes()
    expected = bytearray(4096)
    expected[:8] = b"CANOEFT1"
    struct.pack_into("<QQII", expected, 8, len(raw), len(raw) - 4096, 2, 0)
    expected[0x20:0x40] = hashlib.sha256(
        struct.pack("<QQQQ", 12, 3, 99, 7),
    ).digest()
    assert raw[-4096:] == bytes(expected)


def test_trailer_then_verify_round_trip(tmp_path: Path) -> None:
    image = tmp_path / "efisp.fat"
    image.write_bytes(b"FAT" * 4096)
    runs = ((12, 3), (99, 7))

    fatimg.rewrite_trailer(image, runs)

    assert fatimg.verify_image(image, runs).ok


def test_verify_reports_corrupted_magic(tmp_path: Path) -> None:
    image = tmp_path / "efisp.fat"
    image.write_bytes(b"FAT" * 4096)
    fatimg.rewrite_trailer(image, ())
    raw = bytearray(image.read_bytes())
    raw[-4096] = ord("X")
    image.write_bytes(raw)

    report = fatimg.verify_image(image)

    assert report.mismatches == ("magic",)


def test_verify_reports_wrong_size(tmp_path: Path) -> None:
    image = tmp_path / "efisp.fat"
    image.write_bytes(b"FAT" * 4096)
    fatimg.rewrite_trailer(image, ())
    raw = bytearray(image.read_bytes())
    struct.pack_into("<Q", raw, -4088, 99)
    image.write_bytes(raw)

    report = fatimg.verify_image(image)

    assert report.mismatches == ("file size",)


def test_verify_reports_wrong_run_hash(tmp_path: Path) -> None:
    image = tmp_path / "efisp.fat"
    image.write_bytes(b"FAT" * 4096)
    fatimg.rewrite_trailer(image, ((12, 3),))
    raw = bytearray(image.read_bytes())
    raw[-4064] ^= 1
    image.write_bytes(raw)

    report = fatimg.verify_image(image, ((12, 3),))

    assert report.mismatches == ("run-list hash",)
