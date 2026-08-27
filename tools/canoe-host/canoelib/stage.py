"""Install a prepared Canoe boot root through BDS USB mass storage."""

from __future__ import annotations

import argparse
import shutil
import tempfile
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from . import boottree, massstorage, vendorboot
from .config import Config, ConfigError, verify_config
from .errors import CanoeError
from .layout import GM2P_BYTES, TZMAP_BYTES, Toolkit, require_exact, require_nonempty
from .proc import Completed, run
from .stage_report import stage_report
from .ui import emit, note, run_entry, step

PROG: Final = "canoe install"


@dataclass(frozen=True, slots=True)
class Options:
    """Parsed non-interactive install options."""

    boot_root: Path | None
    slot: str
    mode: int
    vendor_boot: Path | None
    allow_new_signer: bool



def entry(argv: Sequence[str]) -> int:
    """Run canoe install."""
    return run_entry(PROG, _run, argv)


def install(argv: Sequence[str]) -> None:
    """Run install while allowing the interactive wizard to handle its errors."""
    _run(argv)


def _parse_mode(raw: str) -> int:
    """Parse the three mode values accepted by canoe.cfg."""
    if raw not in ("0", "1", "2"):
        raise argparse.ArgumentTypeError("must be 0, 1 or 2")
    return int(raw)


def _parse_slot(raw: str) -> str:
    """Parse a slot letter into the suffix used by the BDS row writer."""
    if raw not in ("a", "b"):
        raise argparse.ArgumentTypeError("must be a or b")
    return f"_{raw}"


def _options(argv: Sequence[str]) -> Options:
    """Parse install options without consulting a device."""
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Install a prepared boot root over BDS USB Mass Storage.",
        epilog=(
            "Use --boot-root for an already-mounted persist export; otherwise canoe starts "
            "fastboot oem mass-storage:persist. The BDS session ends only with Volume Down."
        ),
        exit_on_error=False,
    )
    parser.add_argument("--boot-root", metavar="PATH", help="already-mounted persist/efisp path")
    parser.add_argument("--slot", type=_parse_slot, required=True, metavar="a|b")
    parser.add_argument("--mode", type=_parse_mode, default=1, metavar="0|1|2")
    parser.add_argument("--vendor-boot", type=Path, metavar="IMG")
    parser.add_argument("--allow-new-signer", action="store_true")
    try:
        parsed = parser.parse_args(argv)
    except argparse.ArgumentError as exc:
        raise CanoeError(str(exc)) from exc
    return Options(
        Path(parsed.boot_root) if parsed.boot_root is not None else None,
        parsed.slot,
        parsed.mode,
        parsed.vendor_boot,
        parsed.allow_new_signer,
    )


def _stage_files(toolkit: Toolkit) -> list[tuple[Path, str]]:
    """Return the triplet and optional EFI tools that the Python writer installs."""
    files: list[tuple[Path, str]] = [
        (toolkit.boot_efi, "boot.efi"),
        (toolkit.gm2p, "boot.efi.gm2p"),
        (toolkit.tzmap, "boot.efi.tzmap"),
    ]
    if toolkit.efisp_tools.is_dir():
        files.extend(
            (item, f"tools/{item.name}")
            for item in sorted(toolkit.efisp_tools.iterdir(), key=lambda p: p.name)
            if item.is_file()
        )
    return files


def _stage_local(toolkit: Toolkit, staging: Path) -> None:
    """Copy the validated payload into a private directory."""
    for source, name in _stage_files(toolkit):
        destination = staging / name
        try:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
        except OSError as exc:
            raise CanoeError(f"could not stage {name}: {exc}") from exc
        note(name)


def _check_verify(result: Completed, message: str) -> None:
    """Fail with ``message`` and quote a verifier diagnosis."""
    if result.ok:
        return
    detail = (result.err or result.out).strip()
    raise CanoeError(f"{message}: {detail}" if detail else message)


def _verify_tzmap(toolkit: Toolkit) -> None:
    """Verify the staged map against the unpatched ABL loader when available."""
    if not toolkit.abl_original.is_file():
        note("skipping ABL/tzmap consistency check: ABL_original.efi is unavailable")
        return
    _check_verify(
        run(
            [
                toolkit.tool("abl_tzmap"),
                "verify",
                "--sidecar",
                toolkit.tzmap,
                "--abl",
                toolkit.abl_original,
                "--allow-zero-digest",
            ]
        ),
        "abl_tzmap verify failed",
    )


def _verify_installed_config(path: Path) -> Config:
    """Confirm that the installed config has canonical bytes."""
    try:
        config = verify_config(path)
    except ConfigError as exc:
        raise CanoeError(f"installed canoe.cfg verification failed: {exc}") from exc
    note(f"canoe.cfg verified (generation {config.generation})")
    return config


def _install_mounted(
    toolkit: Toolkit,
    staging: Path,
    options: Options,
) -> tuple[Path, boottree.Receipt]:
    """Install into a mounted root and release the transport afterwards."""
    handle = (
        massstorage.export(toolkit.root)
        if options.boot_root is None
        else massstorage.local_boot_root(options.boot_root)
    )
    try:
        boot_root = Path(handle.boot_root)
        step("Installing the staged boot root")
        receipt = boottree.install_tree(
            boot_root,
            staging,
            mode=options.mode,
            active_slot=options.slot,
            allow_new_signer=options.allow_new_signer,
        )
        _verify_installed_config(boot_root / "canoe.cfg")
        return boot_root, receipt
    finally:
        massstorage.release(handle)


def _run(argv: Sequence[str]) -> None:
    """Run one host-side install against a mounted or newly exported boot root."""
    options = _options(argv)
    toolkit = Toolkit.shipped()
    for path in (toolkit.boot_efi, toolkit.gm2p, toolkit.tzmap):
        require_nonempty(path, f"missing or empty: {path.relative_to(toolkit.root)}")
    require_exact(toolkit.gm2p, GM2P_BYTES, "boot.efi.gm2p")
    require_exact(toolkit.tzmap, TZMAP_BYTES, "boot.efi.tzmap")
    _verify_tzmap(toolkit)

    patched_vendor: Path | None = None
    with tempfile.TemporaryDirectory(prefix="canoe-stage-") as directory:
        staging = Path(directory)
        _stage_local(toolkit, staging)
        if options.vendor_boot is not None:
            patched_vendor = toolkit.root / "work" / "vendor_boot_patched.img"
            vendorboot.patch_cmdline(options.vendor_boot, patched_vendor)
        if options.boot_root is None:
            step("Exporting persist over USB Mass Storage")
        boot_root, receipt = _install_mounted(toolkit, staging, options)

    emit(
        stage_report(
            destination=str(boot_root),
            mode=options.mode,
            first_install=receipt.first_install,
            vendor_boot=patched_vendor,
        )
    )
