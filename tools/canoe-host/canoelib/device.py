"""Slots, partitions and the persist mount: the device-shaped questions.

Nothing here guesses. An undetectable slot is an error the caller has to
resolve with an explicit `--slot`, because writing the wrong slot is a boot
risk, and a slotted partition is never silently substituted with the bare name.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Final

from .adb import Adb
from .errors import CanoeError
from .layout import size_of
from .ui import note

BY_NAME: Final = "/dev/block/by-name"
PERSIST_CANDIDATES: Final = ("/persist", "/mnt/vendor/persist")
CMDLINE_KEY: Final = "androidboot.slot_suffix="


class SlotRole(StrEnum):
    """A slot named by its role rather than by its suffix."""

    ACTIVE = "active"
    INACTIVE = "inactive"


@dataclass(frozen=True, slots=True)
class SlotSuffix:
    """A literal slot suffix, `_a` or `_b`."""

    value: str

    def other(self) -> SlotSuffix:
        """The opposite suffix."""
        match self.value:
            case "_a":
                return SlotSuffix("_b")
            case "_b":
                return SlotSuffix("_a")
            # Untrusted: the suffix came from the device, not from a closed variant.
            case _:
                raise CanoeError(f"could not resolve the inactive slot from '{self.value}'")


SlotRequest = SlotRole | SlotSuffix


def parse_slot(raw: str) -> SlotRequest:
    """Parse a `--slot` value into the request it names."""
    match raw:
        case "" | "active":
            return SlotRole.ACTIVE
        case "inactive":
            return SlotRole.INACTIVE
        case "_a" | "_b":
            return SlotSuffix(raw)
        case "a" | "b":
            return SlotSuffix(f"_{raw}")
        # Untrusted: arbitrary CLI text, not a closed variant.
        case _:
            raise CanoeError(f"--slot must be _a, _b, active or inactive (got '{raw}')")


def detect_slot(adb: Adb) -> SlotSuffix | None:
    """The active slot suffix, or None on a non-A/B layout."""
    raw = adb.shell("getprop ro.boot.slot_suffix").out.strip()
    if not raw:
        tokens = adb.shell("cat /proc/cmdline").out.split()
        raw = next(
            (token[len(CMDLINE_KEY) :] for token in tokens if token.startswith(CMDLINE_KEY)),
            "",
        )
    return SlotSuffix(raw.strip()) if raw.strip() else None


def resolve_part(adb: Adb, base: str, slot: SlotSuffix | None, *, hint: str = "") -> str:
    """The by-name path for `base` on `slot`.

    With a slot the slotted name is REQUIRED: falling back to the bare name
    would read or write a different partition than the caller asked for. The
    bare name is used only when there is no slot at all, which is either a
    non-A/B layout or a partition that is never slotted, such as efisp.
    """
    name = f"{base}{slot.value}" if slot is not None else base
    path = f"{BY_NAME}/{name}"
    if not adb.test(f"-e {path}"):
        raise CanoeError(f"{name} partition not found{hint}")
    return path


def dump_part(adb: Adb, dev: str, out: Path) -> None:
    """Copy a whole partition to the host."""
    remote = "/tmp/canoe-dump.img"
    if not adb.shell(f"dd if={dev} of={remote} bs=4M").ok:
        raise CanoeError(f"could not read {dev}")
    adb.pull(remote, out)
    adb.shell(f"rm -f {remote}")
    if size_of(out) == 0:
        raise CanoeError(f"dump of {dev} is empty")


def find_persist(adb: Adb, want: str | None) -> str:
    """The persist mount point, mounting it if it is not mounted yet."""
    if want:
        if not adb.shell(f"grep -q ' {want} ' /proc/mounts").ok:
            raise CanoeError(f"persist is not mounted at {want}")
        return want
    for candidate in PERSIST_CANDIDATES:
        if adb.shell(f"grep -q ' {candidate} ' /proc/mounts").ok:
            return candidate
    note("not mounted; attempting to mount /persist")
    mount = f"mkdir -p /persist && mount -t ext4 {BY_NAME}/persist /persist"
    if not adb.shell(mount).ok:
        raise CanoeError("could not mount persist; pass --persist PATH")
    if not adb.shell("grep -q ' /persist ' /proc/mounts").ok:
        raise CanoeError("persist did not mount")
    return "/persist"
