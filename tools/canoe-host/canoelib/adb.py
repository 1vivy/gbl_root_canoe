"""The adb transport: the one place that knows how to talk to the device."""

from __future__ import annotations

import os
import shutil
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from .errors import CanoeError
from .layout import Toolkit, platform_names
from .proc import Completed, run
from .ui import note

# Transports that give us a working shell. A TWRP-derived custom recovery
# reports `recovery`, never `device`.
READY_STATES: Final = frozenset({"device", "recovery", "rescue", "sideload"})
DEFAULT_WAIT_SECONDS: Final = 60


def _wait_seconds() -> int:
    """How long to wait for a transport; overridable for slow enumeration."""
    raw = os.environ.get("CANOE_ADB_WAIT", "")
    return int(raw) if raw.isdigit() else DEFAULT_WAIT_SECONDS


def _locate(toolkit: Toolkit) -> Path:
    """The adb to use: the bundled one when it is runnable, otherwise PATH."""
    for name in platform_names("adb"):
        bundled = toolkit.root / "Platform-Tools" / name
        if bundled.is_file() and (os.name == "nt" or os.access(bundled, os.X_OK)):
            return bundled
    found = shutil.which("adb")
    if found is None:
        raise CanoeError("adb not found (put it on PATH or in ./Platform-Tools/)")
    return Path(found)


@dataclass(frozen=True, slots=True)
class Adb:
    """A resolved adb binary bound to one device."""

    binary: Path
    serial: str | None

    @classmethod
    def connect(cls, toolkit: Toolkit, serial: str | None) -> Adb:
        """Resolve adb, wait for a usable transport, and prove a shell works.

        Deliberately NOT `adb wait-for-device`: that waits for state=device
        specifically, which a custom recovery never reports, so it blocks
        forever in exactly the environment these tools are documented to run
        in. Poll get-state and accept any transport that gives a shell.
        """
        adb = cls(_locate(toolkit), serial)
        limit = _wait_seconds()
        waited = 0
        state = adb.state()
        while state not in READY_STATES and waited < limit:
            time.sleep(1)
            waited += 1
            state = adb.state()
        if state not in READY_STATES:
            seen = state or "none"
            raise CanoeError(
                f"no usable adb transport after {limit}s (state: {seen}); enable ADB in recovery"
            )
        note(f"adb transport: {state}")
        if not adb.shell("true").ok:
            raise CanoeError("no adb shell (enable ADB in recovery)")
        return adb

    def argv(self, *args: str) -> list[str]:
        """The full adb command line for `args`, including the serial selector."""
        prefix = [str(self.binary)]
        if self.serial:
            prefix += ["-s", self.serial]
        return prefix + list(args)

    def state(self) -> str:
        """The transport state, or an empty string when adb reports nothing."""
        return run(self.argv("get-state")).out.strip()

    def shell(self, command: str) -> Completed:
        """Run `command` in the device shell."""
        return run(self.argv("shell", command))

    def test(self, expression: str) -> bool:
        """True when `[ <expression> ]` succeeds on the device."""
        return self.shell(f"[ {expression} ]").ok

    def push(self, local: Path, remote: str) -> None:
        """Copy a host file to the device."""
        if not run(self.argv("push", str(local), remote)).ok:
            raise CanoeError(f"adb push failed: {local}")

    def pull(self, remote: str, local: Path) -> None:
        """Copy a device file to the host."""
        local.parent.mkdir(parents=True, exist_ok=True)
        if not run(self.argv("pull", remote, str(local))).ok:
            raise CanoeError(f"adb pull failed: {remote}")

    def size(self, remote: str) -> int | None:
        """Byte length of a device file, or None when the probe printed nothing.

        The sentinel is the point. The shipped batch driver coerced a silent
        probe into a value and reported the literal string " =" as the size it
        had read, twice, in two releases. A probe that said nothing must stay
        distinguishable from a probe that said zero.
        """
        text = self.shell(f"wc -c < {remote}").out.strip()
        return int(text) if text.isdigit() else None

    def partition_bytes(self, dev: str) -> int:
        """Size of a block device, which must be readable to write it safely."""
        text = self.shell(f"blockdev --getsize64 {dev}").out.strip()
        if not text.isdigit():
            raise CanoeError(f"could not read the size of {dev}")
        return int(text)
