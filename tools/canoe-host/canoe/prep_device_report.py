"""Closing operator report for standalone device preparation."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class PrepDeviceSummary:
    """Source description and vulnerability result for the report."""

    source: str
    gbl_patched: bool


def prep_device_report(summary: PrepDeviceSummary) -> str:
    """Build the standalone preparation report."""
    lines = [
        "",
        "========================================",
        "canoe_prep_device: done.",
        "",
        f"Derived from {summary.source}:",
        "  efisp/boot.efi          patched ABL loader",
        "  efisp/boot.efi.gm2p     KeyMint profile for the matching vbmeta",
        "  efisp/boot.efi.tzmap    ABL-derived TrustZone map",
        "",
    ]
    if summary.gbl_patched:
        lines.extend(
            (
                "The source ABL carries the GBL vulnerability.",
                "",
                "If it was pulled from the device, the abl partition is already vulnerable and no",
                "ABL flash is needed:",
                "",
                "  ./canoe_stage",
                "",
                "If this ABL is an older downgrade image while the device runs newer firmware,",
                "check that --vbmeta came from the SAME build; a mismatched boot.efi/.gm2p pair",
                "is the one thing this step cannot detect for you.",
            ),
        )
    else:
        lines.extend(
            (
                "The source ABL does NOT carry the GBL vulnerability. The sidecars above are",
                "still correct - they describe the stock pair - but the abl partition has to hold",
                "a vulnerable ABL for the chain to load:",
                "",
                "  ./canoe_stage",
                "  fastboot flash abl <vulnerable>.img",
            ),
        )
    lines.append("========================================")
    return "\n".join(lines)
