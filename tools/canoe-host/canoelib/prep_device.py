"""Derive the canoe boot chain from a device or a supplied stock pair."""

from __future__ import annotations

import argparse
import shutil
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final, assert_never

from . import build
from .adb import Adb
from .device import (
    SlotRequest,
    SlotRole,
    SlotSuffix,
    detect_slot,
    dump_part,
    parse_slot,
    resolve_part,
)
from .errors import CanoeError
from .layout import GM2P_BYTES, TZMAP_BYTES, Toolkit, require_exact, require_nonempty, size_of
from .prep_device_report import PrepDeviceSummary, prep_device_report
from .ui import emit, note, run_entry, step

PROG: Final = "canoe prep-device"


class _ParsedNamespace(argparse.Namespace):
    """Typed argparse storage for this launcher's options."""

    slot: str = ""
    serial: str | None = None
    abl: str | None = None
    vbmeta: str | None = None
    keep_images: bool = False


@dataclass(frozen=True, slots=True)
class _Options:
    """Parsed standalone preparation options."""

    slot: str
    serial: str | None
    abl: Path | None
    vbmeta: Path | None
    keep_images: bool


def entry(argv: Sequence[str]) -> int:
    """Run canoe prep-device."""
    return run_entry(PROG, _run, argv)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Derive the canoe boot chain from the device's own partitions.",
        epilog=(
            "Run from a custom recovery with ADB enabled, then canoe install. This never "
            "touches the abl partition: making it carry the GBL vulnerability is your own "
            "'fastboot flash abl <vulnerable>.img' step, and boot.efi does not have to match "
            "the abl partition's version. Order matters: boot.efi comes from abl and "
            "boot.efi.gm2p from vbmeta, so pulling both gives a matching pair only while abl "
            "still holds its original image. Run this BEFORE flashing a downgraded ABL."
        ),
        exit_on_error=False,
    )
    parser.add_argument(
        "--slot",
        default="",
        metavar="SLOT",
        help=(
            "source slot: _a, _b, active (default) or inactive. 'inactive' is the slot that "
            "is not booted right now, e.g. the one an adb sideload has just written"
        ),
    )
    parser.add_argument(
        "--abl",
        metavar="IMG",
        help="use this ABL image instead of pulling the partition; requires --vbmeta",
    )
    parser.add_argument(
        "--vbmeta",
        metavar="IMG",
        help="use this vbmeta image instead of pulling the partition; requires --abl",
    )
    parser.add_argument("-s", "--serial", help="adb device serial")
    parser.add_argument(
        "--keep-images", action="store_true", help="keep the pulled images in ./images"
    )
    return parser


def _parse(argv: Sequence[str]) -> _Options:
    args = _ParsedNamespace()
    try:
        _parser().parse_args(argv, args)
    except argparse.ArgumentError as exc:
        raise CanoeError(str(exc)) from exc
    return _Options(
        args.slot,
        args.serial,
        Path(args.abl) if args.abl is not None else None,
        Path(args.vbmeta) if args.vbmeta is not None else None,
        args.keep_images,
    )


def _toolkit_path(toolkit: Toolkit, path: Path) -> Path:
    return path if path.is_absolute() else toolkit.root / path


def _copy(source: Path, destination: Path, message: str) -> None:
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    except OSError as exc:
        raise CanoeError(f"{message}: {exc}") from exc


def _resolve_source(adb: Adb, request: SlotRequest) -> str | None:
    match request:
        case SlotRole.ACTIVE:
            active = detect_slot(adb)
            if active is None:
                note("no slot suffix reported; assuming a non-A/B layout")
                return None
            note(f"active slot: {active.value}")
            return active.value
        case SlotRole.INACTIVE:
            active = detect_slot(adb)
            if active is None:
                message = "--slot inactive needs a detectable active slot; "
                message += "pass --slot _a or _b explicitly"
                raise CanoeError(message)
            # The inactive slot is the one an adb sideload has just written;
            # it is the custom-ROM install source, not the currently booted slot.
            inactive = active.other()
            note(f"active slot: {active.value}; sourcing from the inactive slot {inactive.value}")
            note("(the slot an adb sideload has just written)")
            return inactive.value
        case SlotSuffix(value=value):
            note(f"slot forced to {value}")
            return value
    assert_never(request)


def _pull_pair(toolkit: Toolkit, adb: Adb, options: _Options) -> str:
    request = parse_slot(options.slot)
    selected = _resolve_source(adb, request)
    hint = ", pass --slot explicitly or supply --abl/--vbmeta"
    slot = SlotSuffix(selected) if selected is not None else None
    abl = resolve_part(adb, "abl", slot, hint=hint)
    vbmeta = resolve_part(adb, "vbmeta", slot, hint=hint)
    note(f"abl:    {abl}")
    note(f"vbmeta: {vbmeta}")
    step("Pulling the abl/vbmeta pair")
    dump_part(adb, abl, toolkit.abl_image)
    note(f"images/abl.img: {size_of(toolkit.abl_image)} bytes")
    dump_part(adb, vbmeta, toolkit.vbmeta_image)
    note(f"images/vbmeta.img: {size_of(toolkit.vbmeta_image)} bytes")
    return f"{abl} + {vbmeta}"


def _derive(toolkit: Toolkit) -> build.Derived:
    step("Deriving the boot chain")
    derived = build.derive(toolkit)
    require_nonempty(toolkit.boot_efi, "build did not produce efisp/boot.efi")
    require_exact(toolkit.gm2p, GM2P_BYTES, "efisp/boot.efi.gm2p")
    require_exact(toolkit.tzmap, TZMAP_BYTES, "efisp/boot.efi.tzmap")
    return derived


def _run(argv: Sequence[str]) -> None:
    options = _parse(argv)
    # The pair guard must run before connecting so a partial override cannot
    # silently combine versions from two different firmware generations.
    if (options.abl is None) != (options.vbmeta is None):
        raise CanoeError("--abl and --vbmeta must be given together (they are a matched pair)")
    toolkit = Toolkit.shipped()
    try:
        toolkit.images.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise CanoeError(f"could not create images directory: {exc}") from exc
    # Mixing one supplied image with one pulled image recreates the
    # boot.efi/.gm2p version mismatch this pathway exists to prevent.
    if options.abl is not None and options.vbmeta is not None:
        abl = _toolkit_path(toolkit, options.abl)
        vbmeta = _toolkit_path(toolkit, options.vbmeta)
        if not abl.is_file():
            raise CanoeError(f"ABL image not found: {abl}")
        if not vbmeta.is_file():
            raise CanoeError(f"vbmeta image not found: {vbmeta}")
        step("Using the supplied stock pair")
        _copy(abl, toolkit.abl_image, "could not copy the supplied ABL image")
        _copy(vbmeta, toolkit.vbmeta_image, "could not copy the supplied vbmeta image")
        note(f"abl:    {abl}")
        note(f"vbmeta: {vbmeta}")
        source = "the supplied stock pair"
    else:
        step("Connecting")
        adb = Adb.connect(toolkit, options.serial)
        step("Resolving the source slot")
        source = _pull_pair(toolkit, adb, options)
    # `derive` returns this fact from the patcher output; do not grep the log
    # again because that reintroduces the batch parser's fragile text path.
    derived = _derive(toolkit)
    if not options.keep_images:
        try:
            toolkit.abl_image.unlink(missing_ok=True)
            toolkit.vbmeta_image.unlink(missing_ok=True)
        except OSError as exc:
            raise CanoeError(f"could not remove pulled images: {exc}") from exc
    emit(prep_device_report(PrepDeviceSummary(source, derived.gbl_patched)))
