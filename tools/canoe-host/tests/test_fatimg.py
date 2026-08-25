"""Behavioral coverage for the host-side efisp.fat builder."""

from __future__ import annotations

import hashlib
import struct
from pathlib import Path

import pytest

from canoe import fat16, fatimg
from canoe.errors import CanoeError


def _stage(root: Path) -> None:
    (root / "tools").mkdir(parents=True)
    for name, data in {
        "boot.efi": b"BOOT" * 300,
        "boot.efi.gm2p": b"GM2P",
        "boot.efi.tzmap": b"TZMAP",
        "BOOTENTRIES": b"entry\n",
        "tools/Reboot.efi": b"reboot",
    }.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def test_build_lists_expected_files_and_lengths(tmp_path: Path) -> None:
    staging = tmp_path / "efisp"
    _stage(staging)
    image = tmp_path / "efisp.fat"

    result = fatimg.build_image(staging, image, 2 * 1024 * 1024)

    assert result.filesystem_path == "pure-python"
    assert dict(fatimg.list_files(image)) == {
        "BOOTENTRIES": 6,
        "boot.efi": 1200,
        "boot.efi.gm2p": 4,
        "boot.efi.tzmap": 5,
        "tools/Reboot.efi": 6,
    }


def test_build_trailer_matches_contract_and_verifies(tmp_path: Path) -> None:
    staging = tmp_path / "efisp"
    _stage(staging)
    image = tmp_path / "efisp.fat"
    fatimg.build_image(staging, image, 2 * 1024 * 1024)

    raw = image.read_bytes()
    trailer = raw[-4096:]
    expected = bytearray(4096)
    expected[:8] = b"CANOEFT1"
    struct.pack_into("<QQII", expected, 8, len(raw), len(raw) - 4096, 0, 0)
    expected[0x20:0x40] = hashlib.sha256(b"").digest()
    assert trailer == bytes(expected)
    assert fatimg.verify_image(image).ok


def test_verify_reports_magic_size_and_hash_mismatches(tmp_path: Path) -> None:
    staging = tmp_path / "efisp"
    _stage(staging)
    image = tmp_path / "efisp.fat"
    fatimg.build_image(staging, image, 2 * 1024 * 1024)

    corrupt = bytearray(image.read_bytes())
    corrupt[-4096] = ord("X")
    corrupt[-4088:-4080] = struct.pack("<Q", 99)
    corrupt[-4064] ^= 1
    image.write_bytes(corrupt)
    report = fatimg.verify_image(image)
    assert not report.ok
    assert {"magic", "file size", "run-list hash"} <= set(report.mismatches)


def test_trailer_rewrite_accepts_new_runs(tmp_path: Path) -> None:
    staging = tmp_path / "efisp"
    _stage(staging)
    image = tmp_path / "efisp.fat"
    fatimg.build_image(staging, image, 2 * 1024 * 1024)
    runs = ((12, 3), (99, 7))

    fatimg.rewrite_trailer(image, runs)

    assert fatimg.verify_image(image, runs).ok
    assert not fatimg.verify_image(image, ((12, 4), (99, 7))).ok


def test_build_refuses_content_that_does_not_fit_requested_size(tmp_path: Path) -> None:
    staging = tmp_path / "efisp"
    staging.mkdir()
    (staging / "large.bin").write_bytes(b"x" * 100_000)

    with pytest.raises(CanoeError, match="does not fit"):
        fatimg.build_image(staging, tmp_path / "efisp.fat", 64 * 1024)


def test_mtools_absent_uses_pure_python_fallback(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    staging = tmp_path / "efisp"
    _stage(staging)
    monkeypatch.setattr(fat16.shutil, "which", lambda _: None)

    result = fatimg.build_image(staging, tmp_path / "efisp.fat", 2 * 1024 * 1024)

    assert result.filesystem_path == "pure-python"
    assert fatimg.list_files(tmp_path / "efisp.fat")[0][0] == "BOOTENTRIES"


def test_extract_round_trip(tmp_path: Path) -> None:
    staging = tmp_path / "efisp"
    _stage(staging)
    image = tmp_path / "efisp.fat"
    fatimg.build_image(staging, image, 2 * 1024 * 1024)
    out = tmp_path / "out"

    fatimg.extract_files(image, out)

    assert (out / "tools/Reboot.efi").read_bytes() == b"reboot"
