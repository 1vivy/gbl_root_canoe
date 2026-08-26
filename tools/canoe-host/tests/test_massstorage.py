"""Tests for the host-side BDS mass-storage adapter."""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path

import pytest

from canoelib import massstorage
from canoelib.errors import CanoeError
from canoelib.proc import Completed

REPO_ROOT = Path(__file__).resolve().parents[3]
INSTALL_SCRIPT = REPO_ROOT / "tools" / "canoe-device" / "canoe_device_install.sh"
BOOT_ENTRY_SCRIPT = REPO_ROOT / "tools" / "canoe-device" / "canoe_boot_entry.sh"


def _stage(staging: Path, *, gm2p_size: int = 120) -> None:
    staging.mkdir()
    (staging / "boot.efi").write_bytes(b"NEW-BOOT-EFI-PAYLOAD")
    (staging / "boot.efi.gm2p").write_bytes(b"G" * gm2p_size)
    (staging / "boot.efi.tzmap").write_bytes(b"T" * 256)


def test_local_boot_root_accepts_mount_and_efisp_directory(tmp_path: Path) -> None:
    """Given either accepted path form, return the same non-owned boot root."""
    mount = tmp_path / "persist"
    mount.mkdir()

    from_mount = massstorage.local_boot_root(mount)
    from_efisp = massstorage.local_boot_root(from_mount.boot_root)

    assert from_mount == massstorage.Export(
        boot_root=from_mount.boot_root, mount=mount, node=None, owned=False
    )
    assert from_mount.boot_root == from_efisp.boot_root
    assert from_efisp.mount == mount
    assert from_efisp.node is None
    assert from_efisp.owned is False



def test_usb_scsi_snapshot_filters_sysfs_by_subsystem(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given USB and non-USB sysfs disks, include only the USB-backed disk."""
    sys_block = tmp_path / "sys" / "block"
    sys_block.mkdir(parents=True)
    usb_disk = sys_block / "sdb"
    usb_disk_device = usb_disk / "device"
    usb_disk_device.mkdir(parents=True)
    usb_subsystem = tmp_path / "usb"
    usb_subsystem.mkdir()
    (usb_disk_device / "subsystem").symlink_to(usb_subsystem, target_is_directory=True)

    scsi_disk = sys_block / "sdc"
    scsi_disk_device = scsi_disk / "device"
    scsi_disk_device.mkdir(parents=True)
    scsi_subsystem = tmp_path / "scsi"
    scsi_subsystem.mkdir()
    (scsi_disk_device / "subsystem").symlink_to(scsi_subsystem, target_is_directory=True)

    monkeypatch.setattr(massstorage, "_SYS_BLOCK", sys_block)

    assert massstorage._usb_scsi_snapshot() == {"sdb"}

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


def test_transaction_runs_real_device_install_and_forwards_done(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """Given valid staged files, run the real shell transaction and forward its marks."""
    staging = tmp_path / "staging"
    _stage(staging)
    boot_root = tmp_path / "persist" / "efisp"
    handle = massstorage.Export(boot_root, boot_root.parent, None, owned=False)

    massstorage.transaction(
        handle,
        staging,
        INSTALL_SCRIPT,
        mode=0,
        active_slot="_a",
        boot_entry=BOOT_ENTRY_SCRIPT,
    )

    output = capsys.readouterr().out
    assert "CANOE-MARK: done" in output
    assert (boot_root / "canoe.cfg").is_file()
    assert "mode 0" in (boot_root / "canoe.cfg").read_text(encoding="ascii")


def test_transaction_rejects_real_install_without_done_mark(tmp_path: Path) -> None:
    """Given invalid staged data, the real transaction exits before its done mark."""
    staging = tmp_path / "staging"
    _stage(staging, gm2p_size=119)
    boot_root = tmp_path / "persist" / "efisp"
    handle = massstorage.Export(boot_root, boot_root.parent, None, owned=False)

    with pytest.raises(CanoeError, match="did not sign off"):
        massstorage.transaction(
            handle,
            staging,
            INSTALL_SCRIPT,
            mode=1,
            active_slot="_b",
            boot_entry=BOOT_ENTRY_SCRIPT,
        )


def test_export_refuses_windows_with_manual_mount_route(monkeypatch: pytest.MonkeyPatch) -> None:
    """Given Windows, refuse discovery and identify the supported manual route."""
    monkeypatch.setattr(massstorage.os, "name", "nt")

    with pytest.raises(CanoeError, match=r"local_boot_root\(\).*WinFsp\+lklfuse"):
        massstorage.export(Path("fastboot"))


def test_release_flushes_and_unmounts_once(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Given an owned mount, release it once and make a second release harmless."""
    calls: list[list[str]] = []

    def fake_run(command: Sequence[str | Path]) -> Completed:
        calls.append([str(part) for part in command])
        return Completed(code=0, out="", err="")

    monkeypatch.setattr(massstorage.proc, "run", fake_run)
    monkeypatch.setattr(
        massstorage.shutil,
        "which",
        lambda name: "/usr/bin/fusermount3" if name == "fusermount3" else None,
    )
    mount = tmp_path / "mount"
    mount.mkdir()
    handle = massstorage.Export(
        mount / "efisp", mount, Path("/dev/sdb"), owned=True, kind="fuse"
    )
    monkeypatch.setattr(massstorage.os.path, "ismount", lambda path: path.exists())
    massstorage.release(handle)
    massstorage.release(handle)

    assert calls == [["sync"], ["/usr/bin/fusermount3", "-u", str(mount)]]
    assert not mount.exists()
