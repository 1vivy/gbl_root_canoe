"""The operator-facing completion report for canoe install."""

from pathlib import Path


def stage_report(
    *,
    destination: str,
    mode: int,
    first_install: bool,
    vendor_boot: Path | None = None,
) -> str:
    """Describe the installed tree and the host bootloader commands."""
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
    lines.extend(
        (
            "",
            "Bootloader bundle (fastboot; flash the ABL only when it lacks the GBL bug):",
            "  fastboot flash abl <vulnerable>.img",
            "  fastboot flash efisp BDS.efi",
        )
    )
    if vendor_boot is not None:
        lines.append("  fastboot flash vendor_boot<slot> work/vendor_boot_patched.img")
    lines.extend(
        (
            "",
            f"canoe.cfg selects Mode {mode} for the installed entry.",
            "Volume Down on the device is the only way to end the BDS mass-storage session.",
            "Reboot to use the new boot chain.",
            "========================================",
        )
    )
    return "\n".join(lines)
