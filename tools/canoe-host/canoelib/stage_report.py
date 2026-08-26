"""The operator-facing completion report for canoe install."""

from typing import Literal


def stage_report(
    *,
    destination: str,
    via: Literal["adb", "mass-storage"],
    mode: int,
    first_install: bool,
) -> str:
    """Describe the installed tree and the host bootloader bundle."""
    lines = [
        "========================================",
        "canoe install: done.",
        "",
        f"Installed under {destination} via {via}:",
        "  boot.efi, boot.efi.gm2p, boot.efi.tzmap, canoe.cfg, tools/",
    ]
    if first_install:
        lines.append("  No previous generation was present (first install).")
    else:
        lines.append("  boot_backup.efi (previous generation, selectable from the BDS menu)")
    lines.extend(
        (
            "",
            "Bootloader bundle (fastboot; flash the ABL only when it lacks the GBL bug):",
            "  fastboot flash abl <vulnerable>.img",
            "  fastboot flash efisp BDS.efi",
            "",
            f"canoe.cfg selects Mode {mode} for the installed entry.",
            "Reboot to use the new boot chain.",
            "========================================",
        )
    )
    return "\n".join(lines)
