"""Probe Super Fastboot identity before device reachability.

This is separate from massstorage.py: that module mounts and exports a block
device, while this one asks the BDS who it is. Only this probe is needed before
a device is even reachable.
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from .massstorage import fastboot_binary

__all__: Final = ("Identity", "identify")


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


def identify(toolkit_root: Path, *, timeout: float = 10.0) -> Identity:
    """Read the BDS identity variables without requiring a connected device."""
    fastboot = fastboot_binary(toolkit_root)
    try:
        results = tuple(
            subprocess.run(  # noqa: S603
                [str(fastboot), "getvar", name],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
                check=False,
            )
            for name in ("canoe-bds", "current-slot")
        )
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return Identity(None, None)
    if any(result.returncode != 0 for result in results):
        return Identity(None, None)
    bds_version = _parse_getvar(results[0].stderr, "canoe-bds")
    current_slot = _parse_getvar(results[1].stderr, "current-slot")
    if current_slot not in ("a", "b"):
        current_slot = None
    return Identity(bds_version, current_slot)
