use std::path::Path;

pub(crate) struct StageReportInput<'a> {
    pub(crate) destination: &'a str,
    pub(crate) mode: Option<u8>,
    pub(crate) first_install: bool,
    pub(crate) vendor_boot: Option<&'a Path>,
    pub(crate) receipt: &'a canoe_bootmgr::InstallReceipt,
}

pub(crate) fn stage_report(input: &StageReportInput<'_>) -> String {
    let StageReportInput {
        destination,
        mode,
        first_install,
        vendor_boot,
        receipt,
    } = input;
    let mut lines = vec![
        "========================================".to_owned(),
        "canoe install: done.".to_owned(),
        String::new(),
        format!("Installed under {destination}:"),
        "  boot.efi, boot.efi.gm2p, boot.efi.tzmap, canoe.cfg, tools/".to_owned(),
    ];
    if *first_install {
        lines.push("  No previous generation was present (first install).".to_owned());
    } else {
        lines.push(
            "  boot_backup.efi (previous generation, selectable from the BDS menu)".to_owned(),
        );
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
    lines.push(mode.map_or_else(
        || "canoe.cfg keeps the mode the boot root already persists.".to_owned(),
        |mode| format!("canoe.cfg selects Mode {mode} for the installed entry."),
    ));
    if !receipt.acknowledged.is_empty() {
        lines.push(format!(
            "Policy acknowledgements: {}",
            receipt.acknowledged.join(", ")
        ));
    }
    if !receipt.warnings.is_empty() {
        lines.push("Policy warnings:".to_owned());
        lines.extend(
            receipt
                .warnings
                .iter()
                .map(|warning| format!("  WARNING: {warning}")),
        );
    }
    lines.extend([
        "Volume Down on the device is the only way to end the BDS mass-storage session.".to_owned(),
        "Reboot to use the new boot chain.".to_owned(),
        "========================================".to_owned(),
    ]);
    lines.join("\n")
}

#[cfg(test)]
mod tests {
    use super::stage_report;

    #[test]
    fn includes_policy_acknowledgements_and_warnings_as_codes() {
        let receipt = canoe_bootmgr::InstallReceipt {
            active_slot: canoe_bootmgr::Slot::A,
            installed: vec![canoe_bootmgr::Slot::A],
            generation: 2,
            signer_changed: false,
            backup_present: true,
            staged: std::path::PathBuf::from("/tmp/staged"),
            loader_bytes: 10,
            loader_sha256: String::new(),
            gm2p_bytes: 120,
            gm2p_sha256: String::new(),
            tzmap_bytes: 256,
            tzmap_sha256: String::new(),
            tools: Vec::new(),
            mode_request: None,
            acknowledged: vec!["P-FORMAT".to_owned()],
            warnings: vec!["P-PROFILE".to_owned()],
        };

        let report = stage_report(&super::StageReportInput {
            destination: "/persist",
            mode: Some(2),
            first_install: false,
            vendor_boot: None,
            receipt: &receipt,
        });

        assert!(report.contains("P-FORMAT"));
        assert!(report.contains("P-PROFILE"));
        assert!(!report.contains("InstallReceipt"));
    }
}
