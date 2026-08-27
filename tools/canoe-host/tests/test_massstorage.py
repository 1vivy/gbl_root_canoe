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


def _usb_scsi_tree(root: Path, gadget: str, *, block: str = "sda") -> Path:
    """Build the sysfs shape a real USB disk has: a SCSI node under a USB device.

    `/sys/block/<name>/device` is a symlink, and its lexical parents are
    `/sys/block/<name>`, `/sys/block`, `/sys`; only the resolved chain reaches
    the usb device carrying idVendor/idProduct. A fixture that hangs
    `subsystem` directly off `device` cannot tell the two apart, which is how a
    guaranteed 60s discovery timeout shipped green.
    """
    usb_bus, scsi_bus = root / "sys" / "bus" / "usb", root / "sys" / "bus" / "scsi"
    for bus in (usb_bus, scsi_bus):
        bus.mkdir(parents=True, exist_ok=True)
    device = root / "sys" / "devices" / "usb8" / "8-2" / "8-2.2"
    interface = device / "8-2.2:1.0"
    scsi = interface / "host12" / "target12:0:0" / "12:0:0:0"
    scsi.mkdir(parents=True)
    vendor, product = gadget.split(":")
    (device / "idVendor").write_text(f"{vendor}\n", encoding="utf-8")
    (device / "idProduct").write_text(f"{product}\n", encoding="utf-8")
    (device / "subsystem").symlink_to(usb_bus, target_is_directory=True)
    (interface / "subsystem").symlink_to(usb_bus, target_is_directory=True)
    (scsi / "subsystem").symlink_to(scsi_bus, target_is_directory=True)
    sys_block = root / "sys" / "block"
    sys_block.mkdir(parents=True, exist_ok=True)
    (sys_block / block).mkdir()
    (sys_block / block / "device").symlink_to(scsi, target_is_directory=True)
    return sys_block


def test_usb_disks_read_identity_off_the_resolved_scsi_under_usb_chain(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given the real sysfs shape, name the USB disk and its gadget id; skip NVMe."""
    sys_block = _usb_scsi_tree(tmp_path, massstorage._MSC_GADGET_ID)
    nvme_bus = tmp_path / "sys" / "bus" / "nvme"
    nvme_bus.mkdir(parents=True)
    controller = tmp_path / "sys" / "devices" / "pci0000:00" / "nvme" / "nvme0"
    controller.mkdir(parents=True)
    (controller / "subsystem").symlink_to(nvme_bus, target_is_directory=True)
    (sys_block / "nvme0n1").mkdir()
    (sys_block / "nvme0n1" / "device").symlink_to(controller, target_is_directory=True)
    monkeypatch.setattr(massstorage, "_SYS_BLOCK", sys_block)

    disks = massstorage._usb_disks()

    assert disks == {"sda": "05c6:f000"}
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




def test_mount_export_creates_and_probes_a_missing_boot_root(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a mounted persist without efisp, create and probe the boot root first."""
    mount = tmp_path / "mount"
    mount.mkdir()
    monkeypatch.setattr(massstorage.tempfile, "mkdtemp", lambda **_: str(mount))
    monkeypatch.setattr(massstorage, "_mount", lambda *_: "fuse")
    monkeypatch.setattr(massstorage, "_detach_automounts", lambda _: None)

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
    monkeypatch.setattr(massstorage, "_detach_automounts", lambda _: None)
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


def test_find_export_names_the_usb_modeswitch_remediation(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given the packaged switch config and no override, name the guard fix."""
    database = tmp_path / "db"
    database.mkdir()
    (database / "05c6:f000").write_text("StandardEject=1\n", encoding="utf-8")
    monkeypatch.setattr(massstorage, "_USB_MODESWITCH_DB", database)
    monkeypatch.setattr(massstorage, "_USB_MODESWITCH_OVERRIDE", tmp_path / "override")
    empty_block = tmp_path / "block"
    empty_block.mkdir()
    monkeypatch.setattr(massstorage, "_SYS_BLOCK", empty_block)

    with pytest.raises(CanoeError, match="DisableSwitching=1"):
        massstorage._find_export(frozenset(), 0)


def test_find_export_omits_the_remediation_once_guarded(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a DisableSwitching override, keep the plain timeout message."""
    database = tmp_path / "db"
    override = tmp_path / "override"
    database.mkdir()
    override.mkdir()
    (database / "05c6:f000").write_text("StandardEject=1\n", encoding="utf-8")
    (override / "05c6:f000").write_text("DisableSwitching=1\n", encoding="utf-8")
    monkeypatch.setattr(massstorage, "_USB_MODESWITCH_DB", database)
    monkeypatch.setattr(massstorage, "_USB_MODESWITCH_OVERRIDE", override)
    empty_block = tmp_path / "block"
    empty_block.mkdir()
    monkeypatch.setattr(massstorage, "_SYS_BLOCK", empty_block)

    with pytest.raises(CanoeError, match=r"never presented a LUN\.$"):
        massstorage._find_export(frozenset(), 0)


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


def test_mounts_of_returns_only_this_nodes_mount_points_unescaped(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given the kernel mount table, match on source and decode octal escapes."""
    mountinfo = tmp_path / "mountinfo"
    mountinfo.write_text(
        "25 1 8:0 / /run/media/vivy/canoe\\040disk rw,relatime shared:1 - ext4 /dev/sda rw\n"
        "26 1 8:16 / /mnt/other rw,relatime - ext4 /dev/sdb rw\n"
        "27 1 0:22 / /proc rw,relatime - proc proc rw\n",
        encoding="utf-8",
    )
    monkeypatch.setattr(massstorage, "_MOUNTINFO", mountinfo)

    assert massstorage._mounts_of(Path("/dev/sda")) == (Path("/run/media/vivy/canoe disk"),)
    assert massstorage._mounts_of(Path("/dev/sdc")) == ()


def test_mount_export_detaches_the_automounted_copy_before_mounting(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given udisks holding the LUN, unmount it so release() owns the final flush."""
    mount = tmp_path / "mount"
    mount.mkdir()
    issued = _record_mount(monkeypatch, ("udisksctl", "umount", "sudo"))
    monkeypatch.setattr(massstorage, "_mounts_of", lambda _: (Path("/run/media/vivy/uuid"),))
    monkeypatch.setattr(massstorage.tempfile, "mkdtemp", lambda **_: str(mount))
    monkeypatch.setattr(massstorage, "_mount", lambda *_: "system")

    handle = massstorage._mount_export(Path("/dev/sda"))

    assert issued == [["/usr/bin/udisksctl", "unmount", "-b", "/dev/sda"]]
    assert handle.mount == mount


def test_detach_automounts_falls_back_to_sudo_umount(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Given a mount udisks disclaims, unmount it with sudo umount instead."""
    issued: list[list[str]] = []

    def run(command: Sequence[object]) -> Completed:
        issued.append([str(part) for part in command])
        return Completed(code=1 if len(issued) == 1 else 0, out="", err="not handled")

    monkeypatch.setattr(massstorage.shutil, "which", lambda name: f"/usr/bin/{name}")
    monkeypatch.setattr(massstorage.proc, "run", run)
    monkeypatch.setattr(massstorage, "_mounts_of", lambda _: (Path("/mnt/persist"),))

    massstorage._detach_automounts(Path("/dev/sda"))

    assert issued == [
        ["/usr/bin/udisksctl", "unmount", "-b", "/dev/sda"],
        ["/usr/bin/sudo", "/usr/bin/umount", "/mnt/persist"],
    ]


def test_export_adopts_a_live_session_without_re_asking_fastboot(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given the BDS already exporting, mount that LUN and never spawn fastboot.

    Inside its export loop the BDS does not answer fastboot, so a second
    `oem mass-storage:` would only burn the discovery timeout, and the LUN a
    previous run left behind is invisible to a new-disk diff.
    """
    toolkit = tmp_path / "toolkit"
    (toolkit / "Platform-Tools").mkdir(parents=True)
    (toolkit / "Platform-Tools" / "fastboot").write_bytes(b"fastboot")
    dev = tmp_path / "dev"
    dev.mkdir()
    (dev / "sda").write_bytes(b"")

    def refuse(*_: object, **__: object) -> None:
        raise AssertionError("fastboot must not be re-issued during a live export")

    monkeypatch.setattr(massstorage, "_DEV_ROOT", dev)
    monkeypatch.setattr(massstorage, "_usb_disks", lambda: {"sda": massstorage._MSC_GADGET_ID})
    monkeypatch.setattr(massstorage, "_mount_export", lambda node: SimpleNamespace(node=node))
    monkeypatch.setattr(massstorage.subprocess, "Popen", refuse)

    handle = massstorage.export(toolkit)

    assert handle.node == dev / "sda"
