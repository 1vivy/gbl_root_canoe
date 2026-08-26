"""Mount a BDS-exported persist volume and run the shared install transaction."""

from __future__ import annotations

import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Final, Literal

from . import proc
from .errors import CanoeError

__all__: Final = ("Export", "export", "local_boot_root", "release", "transaction")


MountKind = Literal["fuse", "system"]


@dataclass(frozen=True, slots=True)
class Export:
    """Describe the mounted boot root and lifecycle ownership.

    ``owned`` means this module mounted it; false means release leaves it alone.
    ``kind`` distinguishes fuse2fs from kernel ext4 for release.
    """

    boot_root: Path
    mount: Path
    node: Path | None
    owned: bool
    kind: MountKind | None = None


_SYS_BLOCK: Final = Path("/sys/block")
_DEV_ROOT: Final = Path("/dev")


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
        mount = path.parent
        boot_root = path
    else:
        mount = path
        boot_root = _ensure_boot_root(mount)

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
    if not result.ok:
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
        boot_root=mount / "efisp", mount=mount, node=node, owned=True, kind=kind
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


def export(fastboot: Path, *, target: str = "persist", timeout: float = 60.0) -> Export:
    """Start BDS mass storage and mount its ext4 persist volume.

    It ends only on device Volume Down, not unplug; do not wait for fastboot.
    """
    if os.name == "nt":
        raise CanoeError("Windows: use local_boot_root() with WinFsp+lklfuse manual mount")
    if not math.isfinite(timeout) or timeout < 0:
        raise CanoeError(
            f"mass-storage discovery timeout must be finite and non-negative: {timeout}"
        )
    fastboot_binary = shutil.which(str(fastboot))
    if fastboot_binary is None:
        raise CanoeError(
            f"fastboot binary not found: {fastboot}; install fastboot or pass its path"
        )
    before = _usb_scsi_snapshot()
    try:
        process = subprocess.Popen(  # noqa: S603 - fastboot handoff remains asynchronous
            [fastboot_binary, "oem", f"mass-storage:{target}"],
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
    if handle.kind == "fuse":
        fusermount3 = shutil.which("fusermount3")
        if fusermount3 is not None:
            return [fusermount3, "-u", handle.mount]
        umount = shutil.which("umount")
        if umount is not None:
            return [umount, handle.mount]
        raise CanoeError("cannot unmount fuse export: install fusermount3 or umount")

    umount, sudo = shutil.which("umount"), shutil.which("sudo")
    if umount is None or sudo is None:
        raise CanoeError("cannot unmount ext4 export: sudo umount is unavailable")
    return [sudo, umount, handle.mount]


def release(handle: Export) -> None:
    """Flush/unmount only; device remains in BDS mass storage until Volume Down."""
    if not handle.owned:
        return
    if not os.path.ismount(handle.mount):
        shutil.rmtree(handle.mount, ignore_errors=True)
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
        failure = _release_command(command, f"could not unmount persist export {handle.mount}")
        if failure is not None:
            failures.append(failure)
    if failures:
        raise CanoeError("; ".join(failures))
    shutil.rmtree(handle.mount, ignore_errors=True)


def transaction(  # noqa: PLR0913
    handle: Export,
    staging: Path,
    script: Path,
    *,
    mode: int,
    active_slot: str,
    boot_entry: Path,
) -> None:
    """Run the shared device transaction against an exported boot root."""
    if mode not in {0, 1, 2}:
        raise CanoeError(f"transaction mode must be 0, 1 or 2, got {mode}")
    if active_slot not in {"_a", "_b"}:
        raise CanoeError(f"transaction active slot must be _a or _b, got {active_slot}")
    if not staging.is_dir():
        raise CanoeError(f"transaction staging directory not found: {staging}")
    if not script.is_file():
        raise CanoeError(f"transaction script not found: {script}")

    environment = os.environ.copy()
    environment["CANOE_MODE"] = str(mode)
    environment["CANOE_ACTIVE_SLOT"] = active_slot
    environment["CANOE_BOOT_ENTRY"] = str(boot_entry)
    try:
        result: subprocess.CompletedProcess[str] = subprocess.run(  # noqa: S603
            ["sh", script, staging, handle.boot_root],  # noqa: S607
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except OSError as exc:
        raise CanoeError(f"could not run install transaction {script}: {exc}") from exc

    combined_output = result.stdout or ""
    _ = sys.stdout.write(combined_output)
    _ = sys.stdout.flush()
    detail = combined_output.strip()
    suffix = f": {detail}" if detail else ""
    if "CANOE-MARK: done" not in combined_output:
        status = f" (exit status {result.returncode})" if result.returncode else ""
        raise CanoeError(
            f"install transaction did not sign off{status}: missing CANOE-MARK: done{suffix}"
        )
    if result.returncode != 0:
        raise CanoeError(
            f"install transaction failed with exit status {result.returncode}{suffix}"
        )
