"""Probe Super Fastboot identity before device reachability.

This is separate from massstorage.py: that module mounts and exports a block
device, while this one asks the BDS who it is. Only this probe is needed before
a device is even reachable.
"""

from __future__ import annotations

import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from .massstorage import fastboot_binary

__all__: Final = ("Identity", "identify")

_RETRY_DELAY: Final = 0.5


@dataclass(frozen=True, slots=True)
class Identity:
    """Fastboot variables that identify a Super Fastboot device."""

    bds_version: str | None
    current_slot: str | None


def _parse_getvar(stderr: str, name: str) -> str | None:
    prefix = f"{name}: "
    for line in stderr.splitlines():
        if line.startswith(prefix):
            value = line[len(prefix) :].strip()
            return None if not value or value.startswith("FAILED") else value
    return None


def _getvar(fastboot: Path, name: str, timeout: float) -> str | None:
    """Read one fastboot variable, retrying a single miss.

    Measured against the OnePlus 15 BDS: the first fastboot command issued
    after the gadget re-enumerates answers "waiting for any device" while the
    very next one succeeds. One miss must not be reported as "not Super
    Fastboot", because the wizard's warning for that defaults to aborting.
    """
    for attempt in range(2):
        try:
            result = subprocess.run(  # noqa: S603
                [str(fastboot), "getvar", name],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
                check=False,
            )
        except (OSError, ValueError, subprocess.TimeoutExpired):
            return None
        if result.returncode == 0:
            value = _parse_getvar(result.stderr, name)
            if value is not None:
                return value
        if attempt == 0:
            time.sleep(_RETRY_DELAY)
    return None


def identify(toolkit_root: Path, *, timeout: float = 10.0) -> Identity:
    """Read the BDS identity variables without requiring a connected device."""
    fastboot = fastboot_binary(toolkit_root)
    slot = _getvar(fastboot, "current-slot", timeout)
    return Identity(
        bds_version=_getvar(fastboot, "canoe-bds", timeout),
        current_slot=slot if slot in ("a", "b") else None,
    )
