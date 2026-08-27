"""Windows disk discovery and WinFsp-backed ext4 mounting."""

from __future__ import annotations

import re
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath
from typing import Final

from . import proc
from .errors import CanoeError

_WINDOWS_DISKS: Final = (
    "Get-Disk | Where-Object BusType -eq 'USB' | Select-Object -ExpandProperty Number"
)


@dataclass(frozen=True, slots=True)
class WindowsExport:
    """The Windows paths and mounter needed to construct a host export."""

    boot_root: PureWindowsPath
    mount: PureWindowsPath
    node: PureWindowsPath
    mounter: Path
    drive: str


def _windows_disks() -> set[int]:
    try:
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", _WINDOWS_DISKS],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        raise CanoeError(f"could not query USB disks with PowerShell: {exc}") from exc
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        raise CanoeError(f"PowerShell USB disk query failed: {detail}")
    return {int(line) for line in result.stdout.splitlines() if line.strip().isdigit()}


def _windows_drive(mounter: Path) -> str:
    result = proc.run([mounter, "status"])
    if not result.ok:
        detail = (result.err or result.out).strip()
        raise CanoeError(f"ext4windows.exe status failed: {detail}")
    occupied = set(re.findall(r"\b([A-Z]):", result.out.upper()))
    for ordinal in range(ord("Z"), ord("A") - 1, -1):
        drive = chr(ordinal) + ":"
        if drive not in occupied:
            return drive
    raise CanoeError("no free Windows drive letter from Z: to A:")


def _mount(node: int, toolkit_root: Path) -> WindowsExport:
    mounter = toolkit_root / "ext4" / "ext4windows.exe"
    fallback = (
        "Windows mounting failed; run ext4windows.exe --scan, mount the volume manually, "
        "then re-run canoe install --boot-root <drive>:\\efisp"
    )
    try:
        drive = _windows_drive(mounter)
        result = proc.run([mounter, "mount", rf"\\.\PhysicalDrive{node}", drive, "--rw"])
        if not result.ok:
            detail = (result.err or result.out).strip()
            raise CanoeError(f"ext4windows.exe mount failed: {detail}")
    except CanoeError as exc:
        raise CanoeError(f"{fallback}. {exc}") from exc
    return WindowsExport(
        PureWindowsPath(f"{drive}\\efisp"),
        PureWindowsPath(f"{drive}\\"),
        PureWindowsPath(rf"\\.\PhysicalDrive{node}"),
        mounter,
        drive,
    )


def export(toolkit_root: Path, target: str, timeout: float, fastboot: Path) -> WindowsExport:
    """Export persist, detect its USB disk, and mount it read-write."""
    before = _windows_disks()
    try:
        process = subprocess.Popen(
            [str(fastboot), "oem", f"mass-storage:{target}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as exc:
        raise CanoeError(f"could not start fastboot mass-storage export: {exc}") from exc
    deadline = time.monotonic() + timeout
    try:
        while True:
            disks = _windows_disks() - before
            if disks:
                return _mount(min(disks), toolkit_root)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise CanoeError(
                    f"mass-storage export did not expose a new USB disk within {timeout:g}s"
                )
            time.sleep(min(0.1, remaining))
    except CanoeError:
        if process.poll() is None:
            process.terminate()
            try:
                _ = process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                process.kill()
                _ = process.wait()
        raise
