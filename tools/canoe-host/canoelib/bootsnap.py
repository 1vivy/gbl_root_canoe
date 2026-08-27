"""The pre-commit snapshot of a boot root, and the restore that undoes a failure.

The boot-root install is a transaction: a rollback that restored only the
loader would leave the old boot.efi beside the new canoe.cfg and the new tools
tree, which is a menu that launches the wrong thing. So everything the commit
can touch is copied aside first, including the *absence* of canoe.cfg -- a
failed first install must not leave a config naming a loader that was never
written.
"""

from __future__ import annotations

import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from .errors import CanoeError

STORE_NAME: Final = ".canoe.hostsnap"
TOOLS_NAME: Final = "tools"
FILES: Final = (
    "boot.efi",
    "boot.efi.gm2p",
    "boot.efi.tzmap",
    "boot_backup.efi",
    "boot_backup.efi.gm2p",
    "boot_backup.efi.tzmap",
    "boot_a.efi",
    "boot_a.efi.gm2p",
    "boot_a.efi.tzmap",
    "boot_b.efi",
    "boot_b.efi.gm2p",
    "boot_b.efi.tzmap",
    "canoe.cfg",
    ".canoe.gen",
)


@dataclass(frozen=True, slots=True)
class Snapshot:
    """What the boot root held before the commit began."""

    root: Path
    store: Path
    present: frozenset[str]
    had_tools: bool


def take(root: Path) -> Snapshot:
    """Copy every file the commit can overwrite into a sibling store."""
    store = root / STORE_NAME
    shutil.rmtree(store, ignore_errors=True)
    try:
        store.mkdir(parents=True)
        present = {name for name in FILES if (root / name).is_file()}
        for name in present:
            _ = shutil.copyfile(root / name, store / name)
        tools = root / TOOLS_NAME
        had_tools = tools.is_dir()
        if had_tools:
            _ = shutil.copytree(tools, store / TOOLS_NAME)
    except OSError as exc:
        shutil.rmtree(store, ignore_errors=True)
        raise CanoeError(f"could not snapshot {root}: {exc}") from exc
    return Snapshot(root=root, store=store, present=frozenset(present), had_tools=had_tools)


def restore(snapshot: Snapshot) -> None:
    """Put the boot root back exactly as `take` found it."""
    for name in FILES:
        target = snapshot.root / name
        if name in snapshot.present:
            _ = shutil.copyfile(snapshot.store / name, target)
        else:
            target.unlink(missing_ok=True)
    tools = snapshot.root / TOOLS_NAME
    shutil.rmtree(tools, ignore_errors=True)
    if snapshot.had_tools:
        _ = shutil.copytree(snapshot.store / TOOLS_NAME, tools)


def discard(snapshot: Snapshot) -> None:
    """Remove the store, leaving no `.canoe.*` temporary behind."""
    shutil.rmtree(snapshot.store, ignore_errors=True)
