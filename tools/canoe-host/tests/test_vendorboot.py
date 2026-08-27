"""Tests for the fixed-offset vendor_boot cmdline amendment."""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from canoelib.errors import CanoeError
from canoelib.vendorboot import BLACKLIST, CMDLINE_BYTES, CMDLINE_OFFSET, patch_cmdline


def _image(path: Path, cmdline: bytes = b"console=ttyS0") -> None:
    data = bytearray(b"X" * 4096)
    data[:8] = b"VNDRBOOT"
    data[CMDLINE_OFFSET : CMDLINE_OFFSET + CMDLINE_BYTES] = cmdline.ljust(CMDLINE_BYTES, b"\0")
    path.write_bytes(data)


def test_patch_cmdline_appends_exact_token_without_resizing(tmp_path: Path) -> None:
    """Given an unpatched header, append the token and preserve every other byte."""
    source = tmp_path / "vendor_boot.img"
    output = tmp_path / "work" / "vendor_boot_patched.img"
    _image(source)

    assert patch_cmdline(source, output) is True
    before, after = source.read_bytes(), output.read_bytes()
    changed = {
        index
        for index, (left, right) in enumerate(zip(before, after, strict=True))
        if left != right
    }
    assert len(after) == len(before) == 4096
    assert len(changed) == 40
    assert changed <= set(range(CMDLINE_OFFSET, CMDLINE_OFFSET + CMDLINE_BYTES))
    field = after[CMDLINE_OFFSET : CMDLINE_OFFSET + CMDLINE_BYTES]
    assert BLACKLIST in field.split(b"\0", 1)[0]


def test_patch_cmdline_is_idempotent(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """Given an already patched header, copy it unchanged and report that state."""
    source = tmp_path / "vendor_boot.img"
    first = tmp_path / "first.img"
    second = tmp_path / "second.img"
    _image(source, b"console=ttyS0 " + BLACKLIST)

    assert patch_cmdline(source, first) is False
    assert patch_cmdline(first, second) is False
    assert first.read_bytes() == second.read_bytes() == source.read_bytes()
    assert "already patched" in capsys.readouterr().out


def test_patch_cmdline_rejects_bad_magic_without_writing(tmp_path: Path) -> None:
    """Given a non-vendor_boot image, refuse it before creating an output."""
    source = tmp_path / "wrong.img"
    source.write_bytes(b"NOTVBOOT" + b"\0" * 4088)
    output = tmp_path / "patched.img"

    with pytest.raises(CanoeError, match="invalid magic"):
        patch_cmdline(source, output)
    assert not output.exists()


def test_patch_cmdline_rejects_an_overfull_field(tmp_path: Path) -> None:
    """Given a full cmdline field, refuse it without touching the output."""
    source = tmp_path / "full.img"
    _image(source, b"x" * (CMDLINE_BYTES - 40))
    output = tmp_path / "patched.img"

    with pytest.raises(CanoeError, match="no room"):
        patch_cmdline(source, output)
    assert not output.exists()


@pytest.mark.skipif(
    "CANOE_VENDOR_BOOT_FIXTURE" not in os.environ,
    reason="set CANOE_VENDOR_BOOT_FIXTURE to run the real-image check",
)
def test_patch_cmdline_real_image_fixture(tmp_path: Path) -> None:
    """Given a real vendor_boot image, amend only its fixed cmdline field."""
    source = Path(os.environ["CANOE_VENDOR_BOOT_FIXTURE"])
    output = tmp_path / "vendor_boot_patched.img"
    patch_cmdline(source, output)

    before, after = source.read_bytes(), output.read_bytes()
    changed = {
        index
        for index, (left, right) in enumerate(zip(before, after, strict=True))
        if left != right
    }
    assert len(after) == len(before)
    assert len(changed) in (0, 40)
    assert changed <= set(range(CMDLINE_OFFSET, CMDLINE_OFFSET + CMDLINE_BYTES))
    field = after[CMDLINE_OFFSET : CMDLINE_OFFSET + CMDLINE_BYTES]
    assert BLACKLIST in field.split(b"\0", 1)[0]
