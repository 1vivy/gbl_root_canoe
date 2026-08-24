"""canoe_build: derive the boot chain from an abl/vbmeta pair.

The loader and both sidecars only ever ship together. `boot.efi` comes from
`abl.img` and `boot.efi.gm2p` from `vbmeta.img`, and the BDS trusts that they
describe the same firmware, so any failure in the pipeline removes all three
rather than leaving a fresh loader beside a stale sidecar.
"""

from __future__ import annotations

import argparse
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Final

from .build_report import build_report
from .errors import CanoeError
from .layout import GM2P_BYTES, TZMAP_BYTES, Toolkit, require_exact, require_nonempty
from .proc import Completed, run
from .ui import emit, run_entry, step

PROG: Final = "canoe_build"
LOADER_NAME: Final = "LinuxLoader.efi"

# patch_abl prints this when the source ABL carries no GBL vulnerability. The
# sidecars are still correct - they describe the stock pair - but the abl
# partition has to hold a vulnerable ABL before the chain can load.
GBL_MISSING_MARK: Final = "Warning: Failed to patch ABL GBL"


@dataclass(frozen=True, slots=True)
class Derived:
    """What a successful derive produced."""

    gbl_patched: bool


def derive(toolkit: Toolkit) -> Derived:
    """Derive `boot.efi` and both sidecars, or leave none of them behind."""
    _clear_outputs(toolkit)
    try:
        return _derive(toolkit)
    except CanoeError:
        _remove_triplet(toolkit)
        raise


def entry(argv: Sequence[str]) -> int:
    """Run canoe_build."""
    return run_entry(PROG, _run, argv)


def _run(argv: Sequence[str]) -> None:
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Derive boot.efi and its sidecars from images/abl.img + images/vbmeta.img.",
        epilog=(
            "Expects a matching stock pair in images/: abl.img supplies boot.efi and "
            "vbmeta.img supplies boot.efi.gm2p, and the BDS trusts that they describe the "
            "same firmware. Outputs efisp/boot.efi, its 120-byte .gm2p profile, its 256-byte "
            ".tzmap map, ABL_original.efi and patch_log.txt. Any failure removes all three "
            "outputs rather than leaving a fresh loader beside a stale sidecar."
        ),
    )
    parser.parse_args(argv)
    toolkit = Toolkit.shipped()
    derived = derive(toolkit)
    emit(build_report(gbl_patched=derived.gbl_patched))


def _derive(toolkit: Toolkit) -> Derived:
    if not toolkit.vbmeta_image.is_file():
        raise CanoeError("matching images/vbmeta.img is required")
    _extract_loader(toolkit)
    log = _patch_loader(toolkit)
    _derive_profile(toolkit)
    _derive_tzmap(toolkit)
    return Derived(gbl_patched=GBL_MISSING_MARK not in log)


def _clear_outputs(toolkit: Toolkit) -> None:
    """Remove every output of a previous run, so nothing stale can be mistaken for fresh."""
    _remove_triplet(toolkit)
    for path in (toolkit.root / LOADER_NAME, toolkit.abl_original, toolkit.patch_log):
        path.unlink(missing_ok=True)


def _remove_triplet(toolkit: Toolkit) -> None:
    for path in toolkit.triplet:
        path.unlink(missing_ok=True)


def _check(result: Completed, message: str) -> None:
    """Fail with `message`, quoting what the tool itself said."""
    if result.ok:
        return
    detail = (result.err or result.out).strip()
    raise CanoeError(f"{message}: {detail}" if detail else message)


def _extract_loader(toolkit: Toolkit) -> None:
    """Lift the unpatched loader out of the ABL image."""
    step("Extracting the loader from images/abl.img")
    _check(
        run([toolkit.tool("extractfv"), "-o", toolkit.root, toolkit.abl_image]),
        "extractfv failed",
    )
    loader = toolkit.root / LOADER_NAME
    if not loader.is_file():
        raise CanoeError(f"extractfv produced no {LOADER_NAME}")
    loader.replace(toolkit.abl_original)


def _patch_loader(toolkit: Toolkit) -> str:
    """Patch the loader into `efisp/boot.efi` and return the combined patch log."""
    step("Patching the loader")
    toolkit.efisp.mkdir(parents=True, exist_ok=True)
    result = run(
        [toolkit.tool("patch_abl"), toolkit.abl_original, toolkit.boot_efi],
        log=toolkit.patch_log,
    )
    emit(result.out.rstrip("\n"))
    _check(result, "patch_abl failed")
    require_nonempty(toolkit.boot_efi, "patch_abl produced no nonempty efisp/boot.efi")
    return result.out


def _derive_profile(toolkit: Toolkit) -> None:
    """Derive and validate the KeyMint profile for the matching vbmeta."""
    step("Deriving the KeyMint profile from images/vbmeta.img")
    tool = toolkit.tool("mode2_profile")
    _check(
        run([tool, "derive", "--vbmeta", toolkit.vbmeta_image, "--out", toolkit.gm2p]),
        "mode2_profile derive failed",
    )
    _check(
        run([tool, "validate", "--input", toolkit.gm2p]),
        "mode2_profile validate failed",
    )
    require_exact(toolkit.gm2p, GM2P_BYTES, "mode2_profile output")


def _derive_tzmap(toolkit: Toolkit) -> None:
    """Derive and validate the TrustZone map from the UNPATCHED loader.

    `--allow-incomplete` is deliberate: an ABL with no recorded RE evidence
    still gets a sidecar carrying the soundly derived identifier flags, so an
    un-analysed device can still install.
    """
    step("Deriving the TrustZone map from the unpatched loader")
    tool = toolkit.tool("abl_tzmap")
    _check(
        run([tool, "derive", toolkit.abl_original, "-o", toolkit.tzmap, "--allow-incomplete"]),
        "abl_tzmap derive failed",
    )
    _check(run([tool, "validate", toolkit.tzmap]), "abl_tzmap validate failed")
    require_exact(toolkit.tzmap, TZMAP_BYTES, "abl_tzmap output")
