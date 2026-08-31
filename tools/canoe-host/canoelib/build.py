"""`canoe build`: derive the boot chain from an abl/vbmeta pair.

The canonical ``canoe-bootmgr build`` command owns extraction, patching,
sidecar derivation, validation, and all-or-nothing cleanup. This module keeps
the host-facing progress messages and report while delegating that pipeline.
"""

from __future__ import annotations

import argparse
import json
import shutil
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from .build_report import build_report
from .errors import CanoeError
from .layout import Toolkit
from .proc import Completed, run
from .ui import emit, run_entry, step

PROG: Final = "canoe build"


@dataclass(frozen=True, slots=True)
class Derived:
    """What a successful derive produced."""

    gbl_patched: bool


def derive(toolkit: Toolkit) -> Derived:
    """Derive ``boot.efi`` and both sidecars through the canonical command."""
    step("Extracting the loader from images/abl.img")
    step("Patching the loader")
    step("Deriving the KeyMint profile from images/vbmeta.img")
    step("Deriving the TrustZone map from the unpatched loader")
    result = run(
        [
            toolkit.tool("canoe-bootmgr"),
            "--json",
            "build",
            "--abl",
            toolkit.abl_image,
            "--vbmeta",
            toolkit.vbmeta_image,
            "--staged",
            toolkit.efisp,
            "--keep-unpatched",
            toolkit.abl_original,
            "--patch-log",
            toolkit.patch_log,
        ]
    )
    if not result.ok:
        raise _failure(result)
    receipt = _parse_receipt(result)
    if toolkit.patch_log.is_file():
        emit(toolkit.patch_log.read_text(encoding="utf-8").rstrip("\n"))
    return Derived(gbl_patched=receipt)


def entry(argv: Sequence[str]) -> int:
    """Run canoe build."""
    return run_entry(PROG, _run, argv)

def _failure(result: Completed) -> CanoeError:
    """Return a host error quoting the canonical command's diagnostic."""
    detail = (result.err or result.out).strip()
    try:
        payload = json.loads(result.out)
    except json.JSONDecodeError:
        pass
    else:
        if isinstance(payload, dict):
            error = payload.get("error")
            if isinstance(error, dict) and isinstance(error.get("message"), str):
                detail = error["message"]
            elif isinstance(error, str):
                detail = error
    return CanoeError(detail or "canoe-bootmgr build failed")


def _parse_receipt(result: Completed) -> bool:
    """Parse the GBL status from a successful canonical build receipt."""
    try:
        payload = json.loads(result.out)
    except json.JSONDecodeError as exc:
        raise CanoeError(f"canoe-bootmgr returned invalid JSON: {exc}") from exc
    if not isinstance(payload, dict) or payload.get("operation") != "build":
        raise CanoeError("canoe-bootmgr returned an unexpected build response")
    if payload.get("ok") is not True or payload.get("kind") != "build":
        raise CanoeError("canoe-bootmgr returned an unsuccessful build response")
    receipt = payload.get("receipt")
    if not isinstance(receipt, dict) or not isinstance(receipt.get("gbl_patched"), bool):
        raise CanoeError("canoe-bootmgr build response has an invalid receipt")
    return receipt["gbl_patched"]


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
    parser.add_argument(
        "--abl",
        type=Path,
        metavar="IMG",
        help="copy IMG into images/abl.img before deriving",
    )
    parser.add_argument(
        "--vbmeta",
        type=Path,
        metavar="IMG",
        help="copy IMG into images/vbmeta.img before deriving",
    )
    parsed = parser.parse_args(argv)
    toolkit = Toolkit.shipped()
    for source, target in ((parsed.abl, toolkit.abl_image), (parsed.vbmeta, toolkit.vbmeta_image)):
        if source is None:
            continue
        if not source.is_file():
            raise CanoeError(f"supplied image is not a file: {source}")
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            if source.resolve() != target.resolve():
                shutil.copyfile(source, target)
        except OSError as exc:
            raise CanoeError(f"could not copy {source} to {target}: {exc}") from exc
    derived = derive(toolkit)
    emit(build_report(gbl_patched=derived.gbl_patched))

