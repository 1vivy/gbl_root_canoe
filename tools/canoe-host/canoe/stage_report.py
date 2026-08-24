"""The operator-facing completion report for canoe_stage."""

from __future__ import annotations


def stage_report(
    *,
    destination: str,
    install_bds: bool,
    mode: int | None,
    first_install: bool,
) -> str:
    """Describe the installed tree, BDS action, and preferred-mode result."""
    lines = [
        "========================================",
        "canoe_stage: done.",
        "",
        f"Installed under {destination}:",
        "  boot.efi, boot.efi.gm2p, boot.efi.tzmap, BOOTENTRIES, tools/",
    ]
    if first_install:
        lines.append("  No previous generation was present (first install).")
    else:
        lines.append("  boot_backup.efi (previous generation, selectable from the BDS menu)")
    if install_bds:
        lines.append("  BDS.efi (written to efisp and byte-for-byte verified)")
    else:
        lines.append("  BDS.efi was not changed (--skip-bds); efisp was left untouched")
    lines.append("")
    if mode is None:
        lines.append("The preferred-mode record was left untouched.")
    else:
        lines.append(f"Preferred boot mode set to {mode}.")
    lines.extend(("Reboot to use the new boot chain.", "========================================"))
    return "\n".join(lines)
