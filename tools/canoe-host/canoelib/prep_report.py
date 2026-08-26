"""Closing operator report for the package preparation pathway."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class PrepSummary:
    """Inputs and substitutions reported after package preparation."""

    package: Path
    package_vbmeta: Path
    grafted_recovery: Path | None
    vulnerable_abl: Path | None
    in_place: bool


def prep_report(summary: PrepSummary) -> str:
    """Build the package preparation report."""
    lines = [
        "",
        "========================================",
        "canoe prep: done.",
        "",
        "Prepared:",
    ]
    if summary.grafted_recovery is not None:
        lines.append(f"  {summary.grafted_recovery}")
    lines.extend(
        (
            "  efisp/boot.efi          patched ABL loader",
            f"  efisp/boot.efi.gm2p     KeyMint profile for {summary.package_vbmeta}",
            "  efisp/boot.efi.tzmap    ABL-derived TrustZone map",
            "  BDS.efi                 superfastboot BDS (written raw to efisp)",
            "",
            "Next:",
        ),
    )
    if summary.in_place:
        lines.extend(
            (
                "  1. Run the package's own flasher (Super_Flasher / RegionalHybrid).",
                (
                    f"     It will pick up the substituted images from {summary.package} "
                    "automatically."
                ),
            ),
        )
    else:
        lines.append(
            "".join(
                (
                    f"  1. Install the prepared images into {summary.package} yourself, ",
                    "or rerun with --in-place:",
                ),
            ),
        )
        if summary.grafted_recovery is not None:
            lines.append(f"       cp {summary.grafted_recovery} {summary.package / 'recovery.img'}")
        if summary.vulnerable_abl is not None:
            lines.append(f"       cp {summary.vulnerable_abl} {summary.package / 'abl.img'}")
        lines.append("     then run the package's own flasher.")
    lines.extend(
        (
            "  2. Boot the custom recovery and enable ADB from its UI.",
            "  3. Run: ./canoe install",
            "========================================",
        ),
    )
    return "\n".join(lines)
