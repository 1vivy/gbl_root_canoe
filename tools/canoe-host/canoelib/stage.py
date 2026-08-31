"""Install a prepared Canoe boot root through BDS USB mass storage."""

from __future__ import annotations

import argparse
import shutil
import tempfile
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from . import bootmgr, massstorage, sfb, vendorboot
from .errors import CanoeError
from .layout import GM2P_BYTES, TZMAP_BYTES, Toolkit, require_exact, require_nonempty
from .proc import Completed, run
from .stage_report import stage_report
from .ui import emit, note, run_entry, step, warn

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
    """Parse the slot letter accepted by canoe-bootmgr."""
    if raw not in ("a", "b"):
        raise argparse.ArgumentTypeError("must be a or b")
    return raw

def _options(argv: Sequence[str]) -> Options:
    """Parse install options without consulting a device."""
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Install a prepared boot root through BDS USB Mass Storage.",
        epilog=(
            "Use --boot-root for a local persist/efisp directory in tests; otherwise canoe "
            "starts fastboot oem mass-storage:persist and passes the exported block device "
            "to canoe-bootmgr without mounting it. The BDS session ends only with Volume Down."
        ),
    )
    parser.add_argument("--boot-root", metavar="PATH", help="local persist/efisp directory")
    parser.add_argument("--slot", type=_parse_slot, required=True, metavar="A|B")
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
    """Verify a pre-existing staged map against its unpatched ABL when available.

    ``canoe install`` can consume a triplet derived in an earlier session, so
    the build command's verification cannot establish this install-time match.
    """
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







def _install_export(
    toolkit: Toolkit,
    staging: Path,
    options: Options,
) -> tuple[str, bootmgr.InstallReceipt]:
    """Run the canonical transaction against a local directory or raw export."""
    handle = (
        massstorage.export(toolkit.root)
        if options.boot_root is None
        else massstorage.local_boot_root(options.boot_root)
    )
    try:
        step("Installing the staged boot root")
        receipt = bootmgr.install(
            toolkit,
            handle,
            staging,
            bootmgr.InstallOptions(
                slot=options.slot,
                mode=options.mode,
                allow_new_signer=options.allow_new_signer,
            ),
        )
        destination = handle.boot_root if handle.backend == "local" else handle.source
        if destination is None:
            raise CanoeError("boot-root export has no destination")
        return str(destination), receipt
    finally:
        massstorage.release(handle)


def _run(argv: Sequence[str]) -> None:
    """Run one host-side install against a local root or raw ext4 source."""
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
            try:
                identity = sfb.identify(toolkit.root)
            except CanoeError as exc:
                warn(f"Could not identify the device with fastboot: {exc}")
            else:
                if identity.bds_version is None:
                    warn(
                        "The device does not look like Super Fastboot; "
                        "fastboot oem mass-storage:persist does not exist outside the BDS."
                    )
            step("Exporting persist over USB Mass Storage")
        destination, receipt = _install_export(toolkit, staging, options)

    emit(
        stage_report(
            destination=destination,
            mode=options.mode,
            first_install=receipt.first_install,
            vendor_boot=patched_vendor,
        )
    )
