use std::path::Path;

pub fn stage_report(destination: &str, mode: u8, first_install: bool, vendor_boot: Option<&Path>) -> String {
    let mut lines = vec![
        "========================================".to_owned(),
        "canoe install: done.".to_owned(),
        String::new(),
        format!("Installed under {destination}:"),
        "  boot.efi, boot.efi.gm2p, boot.efi.tzmap, canoe.cfg, tools/".to_owned(),
    ];
    if first_install {
        lines.push("  No previous generation was present (first install).".to_owned());
    } else {
        lines.push("  boot_backup.efi (previous generation, selectable from the BDS menu)".to_owned());
    }
    lines.extend([
        String::new(),
        "Bootloader bundle (fastboot; flash the ABL only when it lacks the GBL bug):".to_owned(),
        "  fastboot flash abl <vulnerable>.img".to_owned(),
        "  fastboot flash efisp BDS.efi".to_owned(),
    ]);
    if vendor_boot.is_some() {
        lines.push("  fastboot flash vendor_boot<slot> work/vendor_boot_patched.img".to_owned());
    }
    lines.extend([
        String::new(),
        format!("canoe.cfg selects Mode {mode} for the installed entry."),
        "Volume Down on the device is the only way to end the BDS mass-storage session.".to_owned(),
        "Reboot to use the new boot chain.".to_owned(),
        "========================================".to_owned(),
    ]);
    lines.join("\n")
}
