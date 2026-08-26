"""Prepare a firmware package without a shell or platform-specific driver."""

from __future__ import annotations

import argparse
import shutil
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from . import build
from .errors import CanoeError
from .layout import GM2P_BYTES, TZMAP_BYTES, Toolkit, require_exact, require_nonempty
from .prep_report import PrepSummary, prep_report
from .proc import Completed, run
from .ui import emit, note, run_entry, step

PROG: Final = "canoe prep"


class _ParsedNamespace(argparse.Namespace):
    """Typed argparse storage for this launcher's options."""

    package: str | None = None
    recovery: str | None = None
    vulnerable_abl: str | None = None
    work: str | None = None
    in_place: bool = False


@dataclass(frozen=True, slots=True)
class _Options:
    """Parsed package preparation options."""

    package: Path | None
    recovery: Path | None
    vulnerable_abl: Path | None
    work: Path | None
    in_place: bool


@dataclass(frozen=True, slots=True)
class _Substitution:
    """One package image replacement and its operator label."""

    source: Path
    destination: Path
    label: str


@dataclass(frozen=True, slots=True)
class _Inputs:
    """Validated package paths and optional replacement images."""

    package: Path
    package_recovery: Path
    package_abl: Path
    package_vbmeta: Path
    custom: Path | None
    vulnerable: Path | None


def entry(argv: Sequence[str]) -> int:
    """Run canoe prep."""
    return run_entry(PROG, _run, argv)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Prepare a stock firmware package for a gbl_root_canoe install.",
        epilog=(
            "Flashes nothing and reimplements no packaged flasher: it only produces correct "
            "inputs. The sidecars always describe the package's STOCK abl/vbmeta pair, "
            "because that is the pair boot.efi and boot.efi.gm2p must agree with; --abl only "
            "changes which ABL image the flasher writes. Afterwards run the package's own "
            "flasher, then canoe install."
        ),
        exit_on_error=False,
    )
    parser.add_argument(
        "--pkg",
        dest="package",
        metavar="DIR",
        help="firmware image directory (e.g. OOS_FILES_HERE); required",
    )
    parser.add_argument(
        "--recovery",
        metavar="IMG",
        help="custom recovery to graft the package's official recovery vbmeta onto",
    )
    parser.add_argument(
        "--abl",
        dest="vulnerable_abl",
        metavar="IMG",
        help="vulnerable ABL for the flasher to write instead of the package's abl.img",
    )
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="substitute prepared images into --pkg, keeping <name>.img.canoe-orig backups",
    )
    parser.add_argument("--work", metavar="DIR", help="staging directory (default: ./work)")
    return parser


def _parse(argv: Sequence[str]) -> _Options:
    args = _ParsedNamespace()
    try:
        _parser().parse_args(argv, args)
    except argparse.ArgumentError as exc:
        raise CanoeError(str(exc)) from exc
    return _Options(
        Path(args.package) if args.package is not None else None,
        Path(args.recovery) if args.recovery is not None else None,
        Path(args.vulnerable_abl) if args.vulnerable_abl is not None else None,
        Path(args.work) if args.work is not None else None,
        args.in_place,
    )


def _toolkit_path(toolkit: Toolkit, path: Path) -> Path:
    return path if path.is_absolute() else toolkit.root / path


def _copy(source: Path, destination: Path, message: str) -> None:
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    except OSError as exc:
        raise CanoeError(f"{message}: {exc}") from exc


def _remove(path: Path, message: str) -> None:
    try:
        path.unlink(missing_ok=True)
    except OSError as exc:
        raise CanoeError(f"{message}: {exc}") from exc


def _check(result: Completed, message: str) -> None:
    if result.ok:
        return
    detail = (result.err or result.out).strip()
    raise CanoeError(f"{message}: {detail}" if detail else message)


def _graft(toolkit: Toolkit, package_recovery: Path, custom: Path, work: Path) -> Path:
    step(f"Lifting the official recovery vbmeta out of {package_recovery}")
    vbmetas = work / "vbmetas"
    try:
        vbmetas.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise CanoeError(f"could not create vbmeta staging directory: {exc}") from exc
    official = vbmetas / "recovery.vbmeta"
    _remove(official, "could not remove the previous recovery.vbmeta")
    # This is deliberately host-side: lifting the package's official vbmeta
    # must never contact a device or depend on its active slot.
    _check(
        run(
            [toolkit.tool("vbmetabackup"), "-f", package_recovery, "-n", "recovery", "-o", vbmetas],
            cwd=toolkit.root,
        ),
        "failed to extract the official recovery vbmeta",
    )
    require_nonempty(official, "recovery.vbmeta is empty")

    step(f"Grafting it onto {custom}")
    grafted = work / "grafted_recovery.img"
    _remove(grafted, "could not remove the previous grafted recovery")
    _check(
        run([toolkit.tool("vbmetaport"), official, custom, grafted], cwd=toolkit.root),
        "vbmetaport failed",
    )
    # A growing output means the port tool clobbered payload bytes; reject it
    # before any package substitution can make that corruption flashable.
    graft_size = require_nonempty(grafted, "grafted recovery is empty")
    custom_size = custom.stat().st_size
    if graft_size != custom_size:
        raise CanoeError(f"grafted recovery changed size ({custom_size} -> {graft_size})")
    note(f"grafted_recovery.img: {graft_size} bytes (size preserved)")
    return grafted


def _derive(toolkit: Toolkit, package_abl: Path, package_vbmeta: Path) -> None:
    # Derive from the package's stock pair. --abl is a flasher input only and
    # must not change the boot.efi/.gm2p version the sidecars describe.
    step("Deriving the canoe boot chain from the package's stock abl/vbmeta pair")
    try:
        toolkit.images.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise CanoeError(f"could not create images directory: {exc}") from exc
    _copy(package_abl, toolkit.abl_image, "could not copy package abl.img into images")
    _copy(package_vbmeta, toolkit.vbmeta_image, "could not copy package vbmeta.img into images")
    build.derive(toolkit)
    require_nonempty(toolkit.boot_efi, "build did not produce efisp/boot.efi")
    require_exact(toolkit.gm2p, GM2P_BYTES, "efisp/boot.efi.gm2p")
    require_exact(toolkit.tzmap, TZMAP_BYTES, "efisp/boot.efi.tzmap")


def _substitute(item: _Substitution) -> None:
    # Never refresh this backup on a rerun: otherwise stock bytes become the
    # already-substituted image and the operator loses the rollback source.
    backup = Path(f"{item.destination}.canoe-orig")
    if backup.is_file():
        note(f"backup already present: {backup}")
    else:
        _copy(item.destination, backup, f"could not back up {item.destination}")
        note(f"backed up {item.destination} -> {backup}")
    _copy(item.source, item.destination, f"could not install {item.label} into the package")
    note(f"installed {item.label} as {item.destination}")


def _validate(toolkit: Toolkit, options: _Options) -> _Inputs:
    if options.package is None:
        raise CanoeError("--pkg is required")
    package = _toolkit_path(toolkit, options.package)
    if not package.is_dir():
        raise CanoeError(f"package directory not found: {package}")
    toolkit.tool("vbmetabackup")
    toolkit.tool("vbmetaport")
    package_recovery = package / "recovery.img"
    package_abl = package / "abl.img"
    package_vbmeta = package / "vbmeta.img"
    if not package_abl.is_file():
        raise CanoeError(f"package is missing abl.img: {package_abl}")
    if not package_vbmeta.is_file():
        raise CanoeError(f"package is missing vbmeta.img: {package_vbmeta}")
    custom = _toolkit_path(toolkit, options.recovery) if options.recovery is not None else None
    vulnerable = (
        _toolkit_path(toolkit, options.vulnerable_abl)
        if options.vulnerable_abl is not None
        else None
    )
    if custom is not None:
        if not custom.is_file():
            raise CanoeError(f"custom recovery not found: {custom}")
        if not package_recovery.is_file():
            raise CanoeError(
                "package is missing recovery.img, so its official vbmeta cannot be lifted"
            )
    if vulnerable is not None and not vulnerable.is_file():
        raise CanoeError(f"vulnerable ABL not found: {vulnerable}")
    return _Inputs(package, package_recovery, package_abl, package_vbmeta, custom, vulnerable)


def _workdir(toolkit: Toolkit, configured: Path | None) -> Path:
    work = _toolkit_path(toolkit, configured) if configured is not None else toolkit.root / "work"
    try:
        work.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise CanoeError(f"could not create work directory: {work}: {exc}") from exc
    return work


def _run(argv: Sequence[str]) -> None:
    options = _parse(argv)
    toolkit = Toolkit.shipped()
    inputs = _validate(toolkit, options)
    work = _workdir(toolkit, options.work)
    grafted = (
        _graft(toolkit, inputs.package_recovery, inputs.custom, work)
        if inputs.custom is not None
        else None
    )
    _derive(toolkit, inputs.package_abl, inputs.package_vbmeta)
    if options.in_place:
        step(f"Substituting prepared images into {inputs.package}")
        if grafted is not None:
            _substitute(_Substitution(grafted, inputs.package_recovery, "grafted recovery"))
        if inputs.vulnerable is not None:
            _substitute(_Substitution(inputs.vulnerable, inputs.package_abl, "vulnerable ABL"))
        if grafted is None and inputs.vulnerable is None:
            note("nothing to substitute (no --recovery, no --abl)")
    emit(
        prep_report(
            PrepSummary(
                inputs.package,
                inputs.package_vbmeta,
                grafted,
                inputs.vulnerable,
                options.in_place,
            ),
        ),
    )
