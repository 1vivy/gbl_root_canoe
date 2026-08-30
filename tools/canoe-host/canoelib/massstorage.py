"""Discover a BDS export without mounting it.

The exported LUN is an ext4 block device, not a host filesystem.  The host
keeps USB and fastboot discovery here, then passes the source directly to
``canoe-bootmgr``.  ``local_boot_root`` is deliberately the only directory
backend and exists for local tests and already-mounted operator workflows.
"""

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

from .errors import CanoeError
from .layout import platform_names
from .massstorage_windows import export as export_windows
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


_SYS_BLOCK: Final = Path("/sys/block")
_DEV_ROOT: Final = Path("/dev")
_USB_MODESWITCH_OVERRIDE: Final = Path("/etc/usb_modeswitch.d")
_MSC_GADGET_ID: Final = "05c6:f000"
# The BDS rewrites the resident export driver's presentation to this Canoe
# identity before the session starts.  The stock identity remains accepted
# because older firmware can retain its own USB presentation.
_MSC_CANOE_GADGET_ID: Final = "1209:ca0e"
_MSC_GADGET_IDS: Final = frozenset({_MSC_GADGET_ID, _MSC_CANOE_GADGET_ID})


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


def _usb_gadget(device: Path) -> Path | None:
    """Return the USB device node behind a sysfs block device, if any."""
    try:
        resolved = device.resolve(strict=True)
    except OSError:
        return None
    for node in (resolved, *resolved.parents):
        if node.name == "sys" or node == Path(node.root):
            break
        try:
            subsystem = (node / "subsystem").resolve(strict=True)
        except OSError:
            continue
        if subsystem.name == "usb" and (node / "idVendor").is_file():
            return node
    return None


def _gadget_id(gadget: Path) -> str | None:
    """Read a USB device's ``vid:pid`` in lowercase sysfs spelling."""
    try:
        vendor = (gadget / "idVendor").read_text(encoding="utf-8").strip()
        product = (gadget / "idProduct").read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return f"{vendor.lower()}:{product.lower()}"


def _usb_disks() -> dict[str, str | None]:
    """Map every USB-backed whole disk to its gadget identity."""
    try:
        entries = sorted(_SYS_BLOCK.iterdir())
    except OSError as exc:
        raise CanoeError(f"could not inspect {_SYS_BLOCK}: {exc}") from exc
    disks: dict[str, str | None] = {}
    for entry in entries:
        if entry.is_dir():
            gadget = _usb_gadget(entry / "device")
            if gadget is not None:
                disks[entry.name] = _gadget_id(gadget)
    return disks


def _exported_disks(disks: dict[str, str | None]) -> tuple[str, ...]:
    """Return USB disks carrying either supported Canoe export identity."""
    return tuple(name for name, gadget in disks.items() if gadget in _MSC_GADGET_IDS)


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


def _stock_identity_hint(disks: dict[str, str | None]) -> str:
    """Name the modeswitch trap only when the stock identity showed up."""
    if _MSC_GADGET_ID not in disks.values():
        return ""
    return (
        f" The session came up with the stock {_MSC_GADGET_ID} identity rather than"
        f" {_MSC_CANOE_GADGET_ID}, so the device used its own driver; stock"
        " usb_modeswitch rules eject that identity mid-scan. Create"
        f" {_USB_MODESWITCH_OVERRIDE / _MSC_GADGET_ID} containing"
        " 'DisableSwitching=1' and retry."
    )


def _find_export(before: frozenset[str], timeout: float) -> Export:
    """Wait for a Canoe export LUN, preferring identity over novelty."""
    deadline = time.monotonic() + timeout
    while True:
        disks = _usb_disks()
        for name in _exported_disks(disks) or tuple(sorted(set(disks) - before)):
            node = _DEV_ROOT / name
            if node.exists():
                return _ext4_export(node)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            remedy = _stock_identity_hint(disks)
            raise CanoeError(
                "mass-storage export did not expose a new USB SCSI disk within"
                f" {timeout:g}s; the device enumerated but never presented a LUN.{remedy}"
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
    fastboot = fastboot_binary(toolkit_root)
    if os.name == "nt":
        windows = export_windows(toolkit_root, target, timeout, fastboot)
        return _ext4_export(windows.node)
    disks = _usb_disks()
    for name in _exported_disks(disks):
        node = _DEV_ROOT / name
        if node.exists():
            note(f"Adopting the mass-storage export already live at {node}")
            return _ext4_export(node)
    try:
        process = subprocess.Popen(  # noqa: S603 - fixed fastboot argv
            [str(fastboot), "oem", f"mass-storage:{target}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as exc:
        raise CanoeError(f"could not start fastboot mass-storage export: {exc}") from exc
    try:
        return _find_export(frozenset(disks), timeout)
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
