"""Tests for USB discovery and direct canoe-ext4 host access."""

from __future__ import annotations

import subprocess
from collections.abc import Sequence
from pathlib import Path
from types import SimpleNamespace

import pytest

from canoelib import massstorage
from canoelib import massstorage_windows
from canoelib.errors import CanoeError


def test_local_boot_root_accepts_mount_and_efisp_directory(tmp_path: Path) -> None:
    """Given either local path form, retain the same non-owned boot root."""
    mount = tmp_path / "persist"
    mount.mkdir()
    from_mount = massstorage.local_boot_root(mount)

    assert from_mount.boot_root is not None
    from_efisp = massstorage.local_boot_root(Path(from_mount.boot_root))
    assert from_mount.backend == "local"
    assert from_mount.boot_root == mount / "efisp"
    assert from_mount.source is None
    assert from_mount.owned is False
    assert from_efisp.boot_root == from_mount.boot_root


def _usb_scsi_tree(root: Path, gadget: str, *, block: str = "sda") -> Path:
    """Build the resolved sysfs shape of a USB SCSI disk."""
    sys_block = root / "sys" / "block"
    block_entry = sys_block / block
    block_entry.mkdir(parents=True)
    usb_device = root / "sys" / "devices" / "usb1" / "1-1"
    usb_device.mkdir(parents=True)
    vendor, product = gadget.split(":", maxsplit=1)
    (usb_device / "idVendor").write_text(vendor, encoding="ascii")
    (usb_device / "idProduct").write_text(product, encoding="ascii")
    (root / "sys" / "bus" / "usb").mkdir(parents=True)
    (usb_device / "subsystem").symlink_to(root / "sys" / "bus" / "usb")
    (block_entry / "device").symlink_to(usb_device, target_is_directory=True)
    return sys_block


def test_usb_disks_read_identity_off_resolved_scsi_chain(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a USB SCSI tree, identify the disk and skip unrelated buses."""
    sys_block = _usb_scsi_tree(tmp_path, massstorage._MSC_GADGET_ID)
    monkeypatch.setattr(massstorage, "_SYS_BLOCK", sys_block)

    disks = massstorage._usb_disks()

    assert disks == {"sda": "05c6:f000"}
    assert massstorage._exported_disks(disks) == ("sda",)


def test_exported_disks_accept_canoe_variant_identity() -> None:
    """Given the Canoe VID/PID, select its raw LUN."""
    disks: dict[str, str | None] = {"sda": massstorage._MSC_CANOE_GADGET_ID}
    assert massstorage._exported_disks(disks) == ("sda",)


def test_local_boot_root_rejects_file_and_non_directory_efisp(tmp_path: Path) -> None:
    """Given a file at either boundary, refuse it with an operator-facing error."""
    persist_file = tmp_path / "persist-file"
    persist_file.write_bytes(b"not a directory")
    with pytest.raises(CanoeError, match="not a directory"):
        massstorage.local_boot_root(persist_file)

    mount = tmp_path / "persist"
    mount.mkdir()
    (mount / "efisp").write_bytes(b"not a directory")
    with pytest.raises(CanoeError, match="boot root is not a directory"):
        massstorage.local_boot_root(mount)


def _helper_binary() -> Path:
    """Build and return the repository's libext2fs helper."""
    helper_root = Path(__file__).resolve().parents[2] / "canoe-ext4"
    result = subprocess.run(
        ["make", "-C", str(helper_root), "canoe-ext4"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    return helper_root / "canoe-ext4"


def _run_helper(
    helper: Path, *args: str, input_bytes: bytes = b""
) -> subprocess.CompletedProcess[bytes]:
    """Invoke one helper operation without a shell or elevated privileges."""
    return subprocess.run(
        [str(helper), *args],
        input=input_bytes,
        capture_output=True,
        check=False,
    )


def test_ext4_helper_reads_writes_and_reads_real_image(tmp_path: Path) -> None:
    """Given a fresh ext4 image, write bytes through the helper and read them back."""
    helper = _helper_binary()
    image = tmp_path / "persist.img"
    image.write_bytes(b"\0" * (32 * 1024 * 1024))
    formatted = subprocess.run(
        ["mke2fs", "-q", "-t", "ext4", "-F", str(image)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert formatted.returncode == 0, formatted.stderr

    created = _run_helper(helper, "--mkdir-p", "mkdir", str(image), "/efisp/tools")
    assert created.returncode == 0, created.stderr.decode()
    payload = b"direct helper payload\n"
    written = _run_helper(
        helper, "write", str(image), "/efisp/tools/marker", input_bytes=payload
    )
    assert written.returncode == 0, written.stderr.decode()
    read_back = _run_helper(helper, "read", str(image), "/efisp/tools/marker")

    assert read_back.returncode == 0, read_back.stderr.decode()
    assert read_back.stdout == payload


def test_find_export_names_modeswitch_remediation_for_stock_identity(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given stock USB identity with no LUN, name its operator remediation."""
    sys_block = _usb_scsi_tree(tmp_path, massstorage._MSC_GADGET_ID)
    monkeypatch.setattr(massstorage, "_SYS_BLOCK", sys_block)
    monkeypatch.setattr(massstorage, "_DEV_ROOT", tmp_path / "dev-missing")

    with pytest.raises(CanoeError, match="DisableSwitching=1"):
        massstorage._find_export(frozenset(), 0)


def test_find_export_keeps_plain_timeout_for_canoe_identity(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given Canoe identity with no LUN, report only the missing device."""
    sys_block = _usb_scsi_tree(tmp_path, massstorage._MSC_CANOE_GADGET_ID)
    monkeypatch.setattr(massstorage, "_SYS_BLOCK", sys_block)
    monkeypatch.setattr(massstorage, "_DEV_ROOT", tmp_path / "dev-missing")

    with pytest.raises(CanoeError, match=r"never presented a LUN\.$"):
        massstorage._find_export(frozenset(), 0)


def test_export_adopts_live_session_without_reasking_fastboot(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a live export, return its raw source without a second fastboot call."""
    toolkit = tmp_path / "toolkit"
    (toolkit / "Platform-Tools").mkdir(parents=True)
    (toolkit / "Platform-Tools" / "fastboot").write_bytes(b"fastboot")
    dev = tmp_path / "dev"
    dev.mkdir()
    (dev / "sda").write_bytes(b"")

    def refuse(*_: Sequence[str], **__: bool) -> None:
        raise AssertionError("fastboot must not be re-issued during a live export")

    monkeypatch.setattr(massstorage, "_DEV_ROOT", dev)
    monkeypatch.setattr(massstorage, "_usb_disks", lambda: {"sda": massstorage._MSC_GADGET_ID})
    monkeypatch.setattr(massstorage.subprocess, "Popen", refuse)

    handle = massstorage.export(toolkit)

    assert handle.backend == "ext4"
    assert handle.source == dev / "sda"
    assert handle.node == dev / "sda"


def test_windows_export_returns_physical_drive_source(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a new Windows USB disk, return its raw PhysicalDrive source."""
    toolkit = tmp_path / "toolkit"
    fastboot = toolkit / "Platform-Tools" / "fastboot"
    fastboot.parent.mkdir(parents=True)
    fastboot.write_bytes(b"fastboot")
    queries = iter(("", "7\n"))

    def powershell(_: Sequence[str | Path], **__: bool) -> SimpleNamespace:
        return SimpleNamespace(returncode=0, stdout=next(queries), stderr="")

    class Process:
        def poll(self) -> int:
            return 0

    monkeypatch.setattr(massstorage.os, "name", "nt")
    monkeypatch.setattr(
        massstorage_windows.shutil, "which", lambda name: f"/mock/{name}"
    )
    monkeypatch.setattr(massstorage.subprocess, "run", powershell)
    monkeypatch.setattr(massstorage.subprocess, "Popen", lambda *_args, **_kwargs: Process())

    handle = massstorage.export(toolkit, timeout=1)

    assert handle.backend == "ext4"
    assert str(handle.source) == r"\\.\PhysicalDrive7"
