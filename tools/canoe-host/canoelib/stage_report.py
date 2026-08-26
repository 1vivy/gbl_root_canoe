"""The operator-facing completion report for canoe install."""

from __future__ import annotations


def stage_report(
    *,
    destination: str,
    install_bds: bool,
    mode: int,
    first_install: bool,
) -> str:
    """Describe the installed tree, BDS action, and declarative menu policy."""
    lines = [
        "========================================",
        "canoe install: done.",
        "",
        f"Installed under {destination}:",
        "  boot.efi, boot.efi.gm2p, boot.efi.tzmap, canoe.cfg, tools/",
    ]
    if first_install:
        lines.append("  No previous generation was present (first install).")
    else:
        lines.append("  boot_backup.efi (previous generation, selectable from the BDS menu)")
    if install_bds:
        lines.append("  BDS.efi (written to efisp and byte-for-byte verified)")
    else:
        lines.append("  BDS.efi was not changed (--skip-bds); efisp was left untouched")
    lines.extend(
        (
            "",
            f"canoe.cfg selects Mode {mode} for the installed entry.",
            "Reboot to use the new boot chain.",
            "========================================",
        )
    )
    return "\n".join(lines)
