"""The host's boot-root install transaction.

`tools/canoe-device/canoe_device_install.sh` is the device-side implementation
of the same transaction and stays the reference for its semantics. This module
reproduces the observable *result* -- the files, the canoe.cfg bytes and the
`CANOE-MARK:` vocabulary -- not the shell's temp-file names, so the two are
compared by `tests/test_parity.py` rather than by shared code.

Host installs never write a partition: the operator flashes the vulnerable ABL
and BDS.efi with fastboot themselves, so there is no `efisp_dev` stage here and
`.canoe.gen` always records `-` for the BDS field.
"""

from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Final, assert_never

from . import bootcfg, bootsnap
from .config import ConfigError, verify_config
from .errors import CanoeError
from .layout import GM2P_BYTES, TZMAP_BYTES, require_exact, require_nonempty
from .ui import emit

SIDECARS: Final = ("", ".gm2p", ".tzmap")
SIGNER_START: Final = 0x38
SIGNER_END: Final = 0x58
GEN_NAME: Final = ".canoe.gen"
SIGNER_MESSAGE: Final = (
    "vbmeta signer changed; the supplied vbmeta was not signed by the same key as the "
    "installed generation. This is expected when moving to or from a custom ROM, and no "
    "tool here can prove which key is the OEM's"
)


@dataclass(frozen=True, slots=True)
class Receipt:
    """What the transaction did, for the report the operator reads."""

    first_install: bool
    generation: int
    signer_changed: bool


@dataclass(frozen=True, slots=True)
class _Drop:
    """Remove one canoe.cfg row."""

    id: str


_Op = bootcfg.Row | _Drop


def install_tree(
    boot_root: Path,
    staged: Path,
    *,
    mode: int | None,
    active_slot: str,
    allow_new_signer: bool = False,
) -> Receipt:
    """Install a staged boot set into a mounted boot root, atomically."""
    if active_slot not in bootcfg.ACTIVE_IDS:
        raise CanoeError(f"active slot must be _a or _b, got {active_slot!r}")
    if mode is not None and mode not in (0, 1, 2):
        raise CanoeError(f"mode must be 0, 1 or 2, got {mode}")
    _validate_staged(staged)
    signer_changed = _signer_gate(boot_root, staged, allow_new_signer=allow_new_signer)

    (boot_root / "tools").mkdir(parents=True, exist_ok=True)
    live = boot_root / "boot.efi"
    first_install = not (live.is_file() and live.stat().st_size > 0)
    snapshot = bootsnap.take(boot_root)
    _mark("first-install" if first_install else "previous-generation-saved")
    try:
        drops = _migrate(boot_root)
        if not first_install:
            _demote(boot_root)
        _commit(boot_root, staged)
        rows = _managed_rows(boot_root, mode=mode, active_slot=active_slot)
        generation = _write_config(boot_root, drops + rows)
        _stamp(boot_root)
    except (CanoeError, ConfigError, OSError) as exc:
        bootsnap.restore(snapshot)
        bootsnap.discard(snapshot)
        _mark("pair-restored")
        raise CanoeError(str(exc)) from exc
    bootsnap.discard(snapshot)
    _mark("done")
    return Receipt(first_install, generation, signer_changed)


def _mark(text: str) -> None:
    emit(f"CANOE-MARK: {text}")


def _validate_staged(staged: Path) -> None:
    """Sizes are contract values the BDS reads at fixed offsets, not sanity checks."""
    _ = require_nonempty(staged / "boot.efi", "staged boot.efi is missing or empty")
    gm2p = staged / "boot.efi.gm2p"
    tzmap = staged / "boot.efi.tzmap"
    require_exact(gm2p, GM2P_BYTES, "boot.efi.gm2p")
    require_exact(tzmap, TZMAP_BYTES, "boot.efi.tzmap")
    _mark(f"staged-set-validated gm2p={GM2P_BYTES} tzmap={TZMAP_BYTES}")


def _signer_gate(boot_root: Path, staged: Path, *, allow_new_signer: bool) -> bool:
    """Compare the installed and staged public-key digests.

    A change detector, not an authenticity proof: nothing in this tree can show
    that a vbmeta is OEM-authored. What it does catch is the running firmware's
    signer changing under the operator without them saying so.

    The source is always `supplied` because the host reads no partitions at all
    -- its vbmeta is the operator's `images/vbmeta.img`. Whether a change may
    proceed is the separate question `allow_new_signer` answers, which is why
    the device script takes CANOE_SIGNER_SOURCE and CANOE_ALLOW_NEW_SIGNER as
    two independent inputs rather than deriving one from the other.
    """
    live = boot_root / "boot.efi.gm2p"
    if not live.is_file():
        return False
    if _signer(live) == _signer(staged / "boot.efi.gm2p"):
        return False
    _mark("signer-changed source=supplied")
    if not allow_new_signer:
        raise CanoeError(SIGNER_MESSAGE)
    return True


def _signer(profile: Path) -> bytes:
    return profile.read_bytes()[SIGNER_START:SIGNER_END]


def _migrate(boot_root: Path) -> list[_Op]:
    """Sweep away the passthrough loaders and the rows that named them.

    The BDS managed-path predicate matches only `boot.efi` and
    `boot_backup.efi`, so a row naming `boot_a.efi`/`boot_b.efi` was always
    passthrough: its configured mode was ignored and its sidecars were never
    read. Nothing writes those loaders after the OTA watcher's removal, so an
    install deletes whatever it left behind rather than documenting it.
    """
    document = bootcfg.load(boot_root / bootcfg.CONFIG_NAME)
    drops: list[_Op] = []
    for slot, entry_id in bootcfg.ACTIVE_IDS.items():
        if not (boot_root / f"boot{slot}.efi").is_file():
            continue
        for suffix in SIDECARS:
            (boot_root / f"boot{slot}.efi{suffix}").unlink(missing_ok=True)
        _mark(f"passthrough-row-migrated id={entry_id}")
        if bootcfg.holds(document, entry_id):
            drops.append(_Drop(entry_id))
    return drops


def _managed_rows(boot_root: Path, *, mode: int | None, active_slot: str) -> list[_Op]:
    """The two rows the BDS can manage, read off the boot root after the rotation.

    The backup row is decided here rather than up front because the loader it
    names is written by the demotion that runs immediately before this.
    """
    document = bootcfg.load(boot_root / bootcfg.CONFIG_NAME)
    config_existed = (boot_root / bootcfg.CONFIG_NAME).exists()
    rows: list[_Op] = [
        bootcfg.Row(
            id=bootcfg.ACTIVE_IDS[active_slot],
            title=bootcfg.ACTIVE_TITLES[active_slot],
            image="boot.efi",
            role="active",
            mode=mode,
            global_mode=None if config_existed else mode,
            default=True,
        )
    ]
    backup = boot_root / "boot_backup.efi"
    if backup.is_file() and backup.stat().st_size > 0:
        rows.append(
            bootcfg.Row(
                id=bootcfg.BACKUP_ID,
                title=bootcfg.BACKUP_TITLE,
                image="boot_backup.efi",
                role="backup",
            )
        )
    elif bootcfg.holds(document, bootcfg.BACKUP_ID):
        rows.append(_Drop(bootcfg.BACKUP_ID))
    return rows


def _demote(boot_root: Path) -> None:
    """Rotate the live generation into boot_backup.

    A backup loader paired with a stale sidecar is worse than one with none, so
    a sidecar the live generation did not have is removed rather than kept.
    """
    for suffix in SIDECARS:
        source = boot_root / f"boot.efi{suffix}"
        target = boot_root / f"boot_backup.efi{suffix}"
        if source.is_file() and source.stat().st_size > 0:
            _copy(source, target)
        else:
            target.unlink(missing_ok=True)


def _commit(boot_root: Path, staged: Path) -> None:
    """Install the staged triplet and tools tree, then flush both to disk."""
    for suffix in SIDECARS:
        _copy(staged / f"boot.efi{suffix}", boot_root / f"boot.efi{suffix}")
    staged_tools = staged / "tools"
    tools = boot_root / "tools"
    if staged_tools.is_dir():
        for item in sorted(staged_tools.iterdir()):
            if item.is_file():
                _copy(item, tools / item.name)
    _mark("committed")
    _sync_dir(tools)
    _sync_dir(boot_root)
    _mark("tree-synced")


def _write_config(boot_root: Path, ops: list[_Op]) -> int:
    """Replay the planned operations, write once, and read the bytes back."""
    path = boot_root / bootcfg.CONFIG_NAME
    document = bootcfg.load(path)
    generation = document.generation
    for op in ops:
        match op:
            case bootcfg.Row():
                generation = bootcfg.upsert(document, op)
                shown = "inherited" if op.mode is None else op.mode
                _mark(f"entry-set id={op.id} role={op.role} mode={shown} generation={generation}")
            case _Drop(id=entry_id):
                generation = bootcfg.remove(document, entry_id)
                _mark(f"entry-removed id={entry_id} generation={generation}")
            case unreachable:
                assert_never(unreachable)
    temporary = boot_root / ".canoe.cfg.host"
    _ = temporary.write_text(bootcfg.render(document), encoding="ascii")
    _sync_file(temporary)
    _ = temporary.replace(path)
    _sync_dir(boot_root)
    _ = verify_config(path)
    return generation


def _stamp(boot_root: Path) -> None:
    """Record what this generation installed. Informational; the BDS never reads it."""
    digests = "|".join(_digest(boot_root / f"boot.efi{suffix}") for suffix in SIDECARS)
    path = boot_root / GEN_NAME
    _ = path.write_text(f"CANOEG1|-|{digests}\n", encoding="ascii")
    _sync_file(path)
    _sync_dir(boot_root)
    _mark("generation-stamped")


def _digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _copy(source: Path, target: Path) -> None:
    _ = target.write_bytes(source.read_bytes())
    _sync_file(target)


def _sync_file(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _sync_dir(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
