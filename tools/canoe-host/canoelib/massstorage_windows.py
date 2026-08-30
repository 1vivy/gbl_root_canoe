"""Windows USB disk discovery for the raw canoe-ext4 backend."""

from __future__ import annotations

import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath
from typing import Final

from .errors import CanoeError

_WINDOWS_DISKS: Final = (
    "Get-Disk | Where-Object BusType -eq 'USB' | Select-Object -ExpandProperty Number"
)


@dataclass(frozen=True, slots=True)
class WindowsExport:
    """The raw PhysicalDrive source selected for canoe-ext4."""

    node: PureWindowsPath


def _powershell() -> str:
    executable = shutil.which("powershell")
    if executable is None:
        raise CanoeError("PowerShell is required for Windows USB disk discovery")
    return executable

def _windows_disks() -> set[int]:
    try:
        result = subprocess.run(  # noqa: S603 - fixed PowerShell query
            [_powershell(), "-NoProfile", "-Command", _WINDOWS_DISKS],
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


def _source(node: int) -> WindowsExport:
    """Convert a Windows disk number into a direct block-device source."""
    return WindowsExport(PureWindowsPath(rf"\\.\PhysicalDrive{node}"))


def _wait_for_new_disk(before: set[int], timeout: float) -> int:
    """Wait for one physical disk added by the fastboot export."""
    deadline = time.monotonic() + timeout
    while True:
        disks = _windows_disks() - before
        if disks:
            return min(disks)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise CanoeError(
                f"mass-storage export did not expose a new USB disk within {timeout:g}s"
            )
        time.sleep(min(0.1, remaining))


def export(toolkit_root: Path, target: str, timeout: float, fastboot: Path) -> WindowsExport:
    """Export persist and return its new PhysicalDrive without mounting it."""
    _ = toolkit_root
    before = _windows_disks()
    try:
        process = subprocess.Popen(  # noqa: S603 - argv list, no shell
            [str(fastboot), "oem", f"mass-storage:{target}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as exc:
        raise CanoeError(f"could not start fastboot mass-storage export: {exc}") from exc
    try:
        node = _wait_for_new_disk(before, timeout)
    except CanoeError:
        if process.poll() is None:
            process.terminate()
            try:
                _ = process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                process.kill()
                _ = process.wait()
        raise
    return _source(node)
