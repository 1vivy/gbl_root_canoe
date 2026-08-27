"""Mount a BDS-exported persist volume for the host install transaction."""

from __future__ import annotations

import math
import os
import shutil
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path, PurePath
from typing import Final, Literal

from . import proc
from .errors import CanoeError
from .layout import platform_names
from .massstorage_windows import export as export_windows

__all__: Final = ("Export", "export", "local_boot_root", "release")

MountKind = Literal["fuse", "system", "windows"]


@dataclass(frozen=True, slots=True)
class Export:
    """Describe the mounted boot root and lifecycle ownership."""

    boot_root: Path | PurePath
    mount: Path | PurePath
    node: Path | PurePath | None
    owned: bool
    kind: MountKind | None = None
    mounter: Path | None = None
    drive: str | None = None


_SYS_BLOCK: Final = Path("/sys/block")
_DEV_ROOT: Final = Path("/dev")
_WINDOWS_DISKS: Final = (
    "Get-Disk | Where-Object BusType -eq 'USB' | Select-Object -ExpandProperty Number"
)


def _write_probe(directory: Path, description: str) -> None:
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=".canoe-write-", dir=directory, delete=True
        ):
            pass
    except OSError as exc:
        raise CanoeError(f"{description} is not writable: {exc}") from exc


def _ensure_boot_root(mount: Path) -> Path:
    boot_root = mount / "efisp"
    if boot_root.exists() and not boot_root.is_dir():
        raise CanoeError(f"boot root is not a directory: {boot_root}")
    if not boot_root.exists():
        try:
            boot_root.mkdir()
        except OSError as exc:
            raise CanoeError(f"could not create boot root {boot_root}: {exc}") from exc
    _write_probe(boot_root, f"boot root {boot_root}")
    return boot_root


def local_boot_root(path: Path) -> Export:
    """Adopt an already-mounted persist filesystem without taking ownership."""
    if not path.is_dir():
        raise CanoeError(f"persist mount is not a directory: {path}")
    _write_probe(path, f"persist mount {path}")
    if path.name == "efisp":
        mount, boot_root = path.parent, path
    else:
        mount, boot_root = path, _ensure_boot_root(path)
    return Export(boot_root=boot_root, mount=mount, node=None, owned=False)


def _is_usb_device(device: Path) -> bool:
    for node in (device, *device.parents):
        try:
            subsystem = (node / "subsystem").resolve(strict=True)
        except OSError:
            subsystem = None
        if subsystem is not None and subsystem.name == "usb":
            return True
        if node == Path("/sys"):
            break
    return False


def _usb_scsi_snapshot() -> set[str]:
    try:
        entries = tuple(_SYS_BLOCK.iterdir())
    except OSError as exc:
        raise CanoeError(f"could not inspect {_SYS_BLOCK}: {exc}") from exc
    return {entry.name for entry in entries if entry.is_dir() and _is_usb_device(entry / "device")}


def _stop_export_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            _ = process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            process.kill()
            _ = process.wait()


def _run_mount(command: list[str | Path], node: Path, mount: Path, kind: MountKind) -> None:
    try:
        result = proc.run(command)
    except CanoeError as exc:
        raise CanoeError(f"mount refused for {node} at {mount}: {exc}") from exc
    if result.ok:
        return
    mounter = "fuse2fs" if kind == "fuse" else "sudo mount -t ext4"
    detail = (result.err or result.out).strip()
    raise CanoeError(f"{mounter} refused {node} at {mount}: {detail}")


def _mount(node: Path, mount: Path) -> MountKind:
    fuse2fs = shutil.which("fuse2fs")
    if fuse2fs is not None:
        _run_mount([fuse2fs, "-o", "rw", node, mount], node, mount, "fuse")
        return "fuse"
    mount_binary, sudo = shutil.which("mount"), shutil.which("sudo")
    if mount_binary is None or sudo is None:
        raise CanoeError("no ext4 mounter available: install fuse2fs or sudo mount")
    _run_mount([sudo, mount_binary, "-t", "ext4", node, mount], node, mount, "system")
    return "system"


def _mount_export(node: Path) -> Export:
    try:
        mount = Path(tempfile.mkdtemp(prefix="canoe-persist-"))
    except OSError as exc:
        raise CanoeError(f"could not create a mount directory: {exc}") from exc
    try:
        kind = _mount(node, mount)
    except CanoeError:
        shutil.rmtree(mount, ignore_errors=True)
        raise
    return Export(
        boot_root=mount / "efisp",
        mount=mount,
        node=node,
        owned=True,
        kind=kind,
    )


def _find_export(before: set[str], timeout: float) -> Export:
    deadline = time.monotonic() + timeout
    while True:
        for name in sorted(_usb_scsi_snapshot() - before):
            node = _DEV_ROOT / name
            if node.exists():
                return _mount_export(node)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise CanoeError(
                f"mass-storage export did not expose a new USB SCSI disk within {timeout:g}s"
            )
        time.sleep(min(0.1, remaining))


def _fastboot_binary(toolkit_root: Path | None = None) -> Path:
    """Resolve the bundled fastboot first, then the operator's PATH copy."""
    root = toolkit_root or Path()
    bundled = tuple(root / "Platform-Tools" / name for name in platform_names("fastboot"))
    for candidate in bundled:
        if candidate.is_file():
            return candidate
    found = shutil.which("fastboot")
    if found is not None:
        return Path(found)
    names = " or ".join(str(path) for path in bundled)
    raise CanoeError(f"fastboot binary not found; expected bundled {names} or fastboot on PATH")



def _windows_export(toolkit_root: Path, target: str, timeout: float, fastboot: Path) -> Export:
    windows = export_windows(toolkit_root, target, timeout, fastboot)
    return Export(
        boot_root=windows.boot_root,
        mount=windows.mount,
        node=windows.node,
        owned=True,
        kind="windows",
        mounter=windows.mounter,
        drive=windows.drive,
    )

def export(toolkit_root: Path, *, target: str = "persist", timeout: float = 60.0) -> Export:
    """Start BDS mass storage and mount its ext4 persist volume."""
    if not math.isfinite(timeout) or timeout < 0:
        raise CanoeError(
            f"mass-storage discovery timeout must be finite and non-negative: {timeout}"
        )
    fastboot = _fastboot_binary(toolkit_root)
    if os.name == "nt":
        return _windows_export(toolkit_root, target, timeout, fastboot)
    before = _usb_scsi_snapshot()
    try:
        process = subprocess.Popen(
            [str(fastboot), "oem", f"mass-storage:{target}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as exc:
        raise CanoeError(f"could not start fastboot mass-storage export: {exc}") from exc
    try:
        return _find_export(before, timeout)
    except CanoeError:
        _stop_export_process(process)
        raise


def _release_command(command: list[str | Path], description: str) -> str | None:
    try:
        result = proc.run(command)
    except CanoeError as exc:
        return f"{description}: {exc}"
    return None if result.ok else f"{description}: {(result.err or result.out).strip()}"


def _unmount_command(handle: Export) -> list[str | Path]:
    mount = Path(str(handle.mount))
    if handle.kind == "fuse":
        fusermount3 = shutil.which("fusermount3")
        if fusermount3 is not None:
            return [fusermount3, "-u", mount]
        umount = shutil.which("umount")
        if umount is not None:
            return [umount, mount]
        raise CanoeError("cannot unmount fuse export: install fusermount3 or umount")
    umount, sudo = shutil.which("umount"), shutil.which("sudo")
    if umount is None or sudo is None:
        raise CanoeError("cannot unmount ext4 export: sudo umount is unavailable")
    return [sudo, umount, mount]

def _release_windows(handle: Export) -> None:
    if handle.mounter is None or handle.drive is None:
        raise CanoeError("Windows export has no ext4windows unmount handle")
    failure = _release_command(
        [handle.mounter, "unmount", handle.drive], "could not unmount Windows persist export"
    )
    if failure is not None:
        raise CanoeError(failure)


def _release_unix(handle: Export) -> None:
    mount = Path(str(handle.mount))
    if not os.path.ismount(mount):
        shutil.rmtree(mount, ignore_errors=True)
        return
    failures: list[str] = []
    failure = _release_command(["sync"], "could not flush the persist mount")
    if failure is not None:
        failures.append(failure)
    try:
        command = _unmount_command(handle)
    except CanoeError as exc:
        failures.append(str(exc))
    else:
        failure = _release_command(command, f"could not unmount persist export {mount}")
        if failure is not None:
            failures.append(failure)
    if failures:
        raise CanoeError("; ".join(failures))
    shutil.rmtree(mount, ignore_errors=True)


def release(handle: Export) -> None:
    """Flush and unmount only; Volume Down on the device ends the BDS session."""
    if not handle.owned:
        return
    if handle.kind == "windows":
        _release_windows(handle)
        return
    _release_unix(handle)
