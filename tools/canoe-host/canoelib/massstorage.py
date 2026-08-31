"""Discover a BDS export through the canonical source detector.

The exported LUN is an ext4 block device, not a host filesystem.  The host
keeps only fastboot session ownership and passes the source selected by
``canoe-bootmgr source detect`` directly to the boot manager.
``local_boot_root`` remains the directory backend for already-mounted
operator workflows and tests.
"""

from __future__ import annotations

import math
import shutil
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path, PurePath
from typing import Final, Literal

from . import bootmgr
from .errors import CanoeError
from .layout import Toolkit, platform_names
from .ui import note

__all__: Final = ("Export", "export", "fastboot_binary", "local_boot_root", "release")

Backend = Literal["local", "ext4"]


@dataclass(frozen=True, slots=True)
class Export:
    """Describe a local directory or an unmounted ext4 source."""

    boot_root: Path | PurePath | None
    source: Path | PurePath | None
    node: Path | PurePath | None
    owned: bool
    backend: Backend




def _write_probe(directory: Path, description: str) -> None:
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=".canoe-write-", dir=directory, delete=True
        ):
            pass
    except OSError as exc:
        raise CanoeError(f"{description} is not writable: {exc}") from exc


def _ensure_boot_root(mount: Path) -> Path:
    """Create and probe the local backend's ``efisp`` directory."""
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
    """Adopt a plain local persist directory without taking ownership."""
    if not path.is_dir():
        raise CanoeError(f"persist root is not a directory: {path}")
    if path.name == "efisp":
        mount, boot_root = path.parent, path
        _write_probe(boot_root, f"boot root {boot_root}")
    else:
        mount, boot_root = path, _ensure_boot_root(path)
    _ = mount
    return Export(boot_root=boot_root, source=None, node=None, owned=False, backend="local")


def _ext4_export(node: Path | PurePath) -> Export:
    """Describe a raw export for the boot-manager ext4 backend."""
    return Export(boot_root=None, source=node, node=node, owned=True, backend="ext4")


def _stop_export_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            _ = process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            process.kill()
            _ = process.wait()


def _detect_export(toolkit_root: Path) -> Export | None:
    """Return the first supported and unmounted block source, if present."""
    for candidate in bootmgr.detect(Toolkit(toolkit_root)):
        if (
            candidate.kind == "block"
            and candidate.identity in ("05c6:f000", "1209:ca0e")
            and candidate.readable
            and candidate.mounted_at is None
        ):
            return _ext4_export(Path(candidate.path))
    return None


def _find_export(toolkit_root: Path, timeout: float) -> Export:
    """Wait for a supported, unmounted block source from canoe-bootmgr."""
    deadline = time.monotonic() + timeout
    while True:
        export_handle = _detect_export(toolkit_root)
        if export_handle is not None:
            return export_handle
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise CanoeError(
                "mass-storage export did not expose an unmounted Canoe block source "
                f"within {timeout:g}s; source detect returned no usable candidate"
            )
        time.sleep(min(0.1, remaining))

def fastboot_binary(toolkit_root: Path | None = None) -> Path:
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


def export(toolkit_root: Path, *, target: str = "persist", timeout: float = 60.0) -> Export:
    """Start or adopt BDS mass storage and return its raw ext4 source."""
    if not math.isfinite(timeout) or timeout < 0:
        raise CanoeError(
            f"mass-storage discovery timeout must be finite and non-negative: {timeout}"
        )
    existing = _detect_export(toolkit_root)
    if existing is not None:
        note(f"Adopting the mass-storage export already live at {existing.source}")
        return existing
    fastboot = fastboot_binary(toolkit_root)
    try:
        process = subprocess.Popen(  # noqa: S603 - fixed fastboot argv
            [str(fastboot), "oem", f"mass-storage:{target}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as exc:
        raise CanoeError(f"could not start fastboot mass-storage export: {exc}") from exc
    try:
        return _find_export(toolkit_root, timeout)
    except CanoeError:
        _stop_export_process(process)
        raise


def release(handle: Export) -> None:
    """Release host-side state; the helper closes each source operation itself."""
    if handle.backend == "local" or not handle.owned:
        return
    # The BDS owns the export session.  Volume Down ends it after the command
    # has returned; there is no host mount or helper handle to tear down.
    return
