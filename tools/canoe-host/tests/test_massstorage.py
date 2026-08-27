"""Tests for the host-side BDS mass-storage adapter."""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path
from types import SimpleNamespace

import pytest

from canoelib import massstorage
from canoelib.errors import CanoeError
from canoelib.proc import Completed


def test_local_boot_root_accepts_mount_and_efisp_directory(tmp_path: Path) -> None:
    """Given either accepted path form, return the same non-owned boot root."""
    mount = tmp_path / "persist"
    mount.mkdir()

    from_mount = massstorage.local_boot_root(mount)
    from_efisp = massstorage.local_boot_root(Path(from_mount.boot_root))

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




def test_mount_export_creates_and_probes_a_missing_boot_root(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a mounted persist without efisp, create and probe the boot root first."""
    mount = tmp_path / "mount"
    mount.mkdir()
    monkeypatch.setattr(massstorage.tempfile, "mkdtemp", lambda **_: str(mount))
    monkeypatch.setattr(massstorage, "_mount", lambda *_: "fuse")

    handle = massstorage._mount_export(Path("/dev/sdb"))

    assert handle.boot_root == mount / "efisp"
    assert Path(handle.boot_root).is_dir()
    assert handle.kind == "fuse"


def test_mount_export_names_root_writer_remedies_on_eacces(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a sudo-mounted root that rejects writes, name the real remedy."""
    mount = tmp_path / "mount"
    mount.mkdir()
    monkeypatch.setattr(massstorage.tempfile, "mkdtemp", lambda **_: str(mount))
    monkeypatch.setattr(massstorage, "_mount", lambda *_: "system")
    monkeypatch.setattr(
        massstorage,
        "_ensure_boot_root",
        lambda *_: (_ for _ in ()).throw(
            CanoeError("boot root is not writable: [Errno 13] Permission denied")
        ),
    )
    monkeypatch.setattr(massstorage.os, "geteuid", lambda: 1000)

    with pytest.raises(
        CanoeError,
        match=(
            r"root-owned ext4 mount.*non-root writer.*Permission denied.*"
            r"re-run canoe as root.*fuse2fs is not a substitute.*journal"
        ),
    ):
        massstorage._mount_export(Path("/dev/sdb"))


def _record_mount(monkeypatch: pytest.MonkeyPatch, available: Sequence[str]) -> list[list[str]]:
    """Stub tool discovery and capture the mount command actually issued."""
    issued: list[list[str]] = []

    def which(name: str) -> str | None:
        return f"/usr/bin/{name}" if name in available else None

    def run(command: Sequence[object]) -> Completed:
        issued.append([str(part) for part in command])
        return Completed(code=0, out="", err="")

    monkeypatch.setattr(massstorage.shutil, "which", which)
    monkeypatch.setattr(massstorage.proc, "run", run)
    return issued


def test_mount_prefers_the_journaled_kernel_driver(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given both mounters, use the kernel one: fuse2fs ignores the ext4 journal."""
    issued = _record_mount(monkeypatch, ("mount", "sudo", "fuse2fs"))

    kind = massstorage._mount(Path("/dev/sdb"), tmp_path)

    assert kind == "system"
    assert issued == [["/usr/bin/sudo", "/usr/bin/mount", "-t", "ext4", "/dev/sdb", str(tmp_path)]]


def test_mount_fallback_uses_fakeroot_so_writes_can_land(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given no sudo, fall back to fuse2fs with fakeroot; plain rw cannot write."""
    issued = _record_mount(monkeypatch, ("fuse2fs",))

    kind = massstorage._mount(Path("/dev/sdb"), tmp_path)

    assert kind == "fuse"
    assert issued == [["/usr/bin/fuse2fs", "-o", "rw,fakeroot", "/dev/sdb", str(tmp_path)]]


def _windows_toolkit(tmp_path: Path) -> Path:
    toolkit = tmp_path / "toolkit"
    (toolkit / "Platform-Tools").mkdir(parents=True)
    (toolkit / "Platform-Tools" / "fastboot.exe").write_bytes(b"fastboot")
    (toolkit / "ext4").mkdir()
    (toolkit / "ext4" / "ext4windows.exe").write_bytes(b"ext4")
    return toolkit


def test_windows_export_mounts_the_new_disk_read_write(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a newly exported USB disk, mount it with the bundled RW command."""
    toolkit = _windows_toolkit(tmp_path)
    disk_queries = iter(("", "7\n"))
    mount_calls: list[list[str]] = []

    def powershell(command: Sequence[str | Path], **_: bool) -> SimpleNamespace:
        assert list(command[:3]) == ["powershell", "-NoProfile", "-Command"]
        return SimpleNamespace(returncode=0, stdout=next(disk_queries), stderr="")

    class FakeProcess:
        def poll(self) -> int:
            return 0

    def tool_run(command: Sequence[str | Path]) -> Completed:
        mount_calls.append([str(part) for part in command])
        if str(command[1]) == "status":
            return Completed(0, "", "")
        return Completed(0, "", "")

    monkeypatch.setattr(massstorage.os, "name", "nt")
    monkeypatch.setattr(massstorage.subprocess, "run", powershell)
    monkeypatch.setattr(massstorage.subprocess, "Popen", lambda *args, **kwargs: FakeProcess())
    monkeypatch.setattr(massstorage.proc, "run", tool_run)
    handle = massstorage.export(toolkit, timeout=1)

    assert mount_calls == [
        [str(toolkit / "ext4" / "ext4windows.exe"), "status"],
        [
            str(toolkit / "ext4" / "ext4windows.exe"),
            "mount",
            r"\\.\PhysicalDrive7",
            "Z:",
            "--rw",
        ],
    ]
    assert str(handle.boot_root) == r"Z:\efisp"
    assert handle.kind == "windows"


def test_windows_mount_failure_names_the_manual_scan_recovery(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given an ext4windows failure, give the exact scan and manual-mount recovery."""
    toolkit = _windows_toolkit(tmp_path)
    disk_queries = iter(("", "9\n"))

    def powershell(command: Sequence[str | Path], **_: bool) -> SimpleNamespace:
        return SimpleNamespace(returncode=0, stdout=next(disk_queries), stderr="")

    class FakeProcess:
        def poll(self) -> int:
            return 0

    def tool_run(command: Sequence[str | Path]) -> Completed:
        if str(command[1]) == "status":
            return Completed(0, "", "")
        return Completed(1, "", "cannot mount")

    monkeypatch.setattr(massstorage.os, "name", "nt")
    monkeypatch.setattr(massstorage.subprocess, "run", powershell)
    monkeypatch.setattr(
        massstorage.subprocess, "Popen", lambda *args, **kwargs: FakeProcess()
    )
    monkeypatch.setattr(massstorage.proc, "run", tool_run)
    error = (
        r"run ext4windows\.exe --scan.*re-run canoe install "
        r"--boot-root <drive>:\\efisp"
    )
    with pytest.raises(CanoeError, match=error):
        massstorage.export(toolkit, timeout=1)



def test_release_flushes_and_unmounts_once(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
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
