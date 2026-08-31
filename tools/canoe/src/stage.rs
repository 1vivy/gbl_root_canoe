use std::fs;
use std::path::{Path, PathBuf};
use std::time::Duration;

use crate::error::CanoeError;
use crate::layout::{require_exact, require_nonempty, Toolkit, GM2P_BYTES, TZMAP_BYTES};
use crate::local::{local_boot_root, TempDir};
use crate::stage_report::stage_report;
use crate::ui::{emit, note, step, warn};

#[derive(Clone, Debug)]
pub struct InstallOptions {
    pub boot_root: Option<PathBuf>,
    pub slot: String,
    pub mode: u8,
    pub vendor_boot: Option<PathBuf>,
    pub allow_new_signer: bool,
}


fn parse_value(args: &[String], index: &mut usize, flag: &str) -> Result<String, CanoeError> {
    *index += 1;
    args.get(*index)
        .cloned()
        .ok_or_else(|| CanoeError::message(format!("argument {flag} requires a value")))
}

pub fn parse(args: &[String]) -> Result<InstallOptions, CanoeError> {
    let mut boot_root = None;
    let mut slot = None;
    let mut mode = 1;
    let mut vendor_boot = None;
    let mut allow_new_signer = false;
    let mut index = 0;
    while index < args.len() {
        match args[index].as_str() {
            "--boot-root" => boot_root = Some(PathBuf::from(parse_value(args, &mut index, "--boot-root")?)),
            "--slot" => slot = Some(parse_value(args, &mut index, "--slot")?),
            "--mode" => {
                let raw = parse_value(args, &mut index, "--mode")?;
                mode = raw.parse::<u8>().map_err(|_| CanoeError::message("mode must be 0, 1 or 2"))?;
            }
            "--vendor-boot" => vendor_boot = Some(PathBuf::from(parse_value(args, &mut index, "--vendor-boot")?)),
            "--allow-new-signer" => allow_new_signer = true,
            "-h" | "--help" => return Err(CanoeError::message("install help is provided by canoe --help")),
            flag => return Err(CanoeError::message(format!("unexpected argument: {flag}"))),
        }
        index += 1;
    }
    let slot = slot.ok_or_else(|| CanoeError::message("the following arguments are required: --slot"))?;
    if slot != "a" && slot != "b" {
        return Err(CanoeError::message("slot must be a or b"));
    }
    if mode > 2 {
        return Err(CanoeError::message("mode must be 0, 1 or 2"));
    }
    Ok(InstallOptions { boot_root, slot, mode, vendor_boot, allow_new_signer })
}


fn stage_files(toolkit: &Toolkit, staging: &Path) -> Result<(), CanoeError> {
    let files = toolkit.triplet();
    for (source, name) in files.into_iter().zip(["boot.efi", "boot.efi.gm2p", "boot.efi.tzmap"]) {
        copy_staged(&source, &staging.join(name), name)?;
    }
    let tools = toolkit.efisp_tools();
    if tools.is_dir() {
        let mut entries = fs::read_dir(&tools)
            .map_err(|error| CanoeError::message(format!("could not read efisp/tools: {error}")))?
            .filter_map(Result::ok)
            .filter(|entry| entry.path().is_file())
            .collect::<Vec<_>>();
        entries.sort_by_key(|entry| entry.file_name());
        for entry in entries {
            let name = format!("tools/{}", entry.file_name().to_string_lossy());
            copy_staged(&entry.path(), &staging.join(&name), &name)?;
        }
    }
    Ok(())
}

fn copy_staged(source: &Path, destination: &Path, name: &str) -> Result<(), CanoeError> {
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::copy(source, destination).map_err(|error| {
        CanoeError::message(format!("could not stage {name}: {error}"))
    })?;
    note(name);
    Ok(())
}

fn validate_triplet(toolkit: &Toolkit) -> Result<(), CanoeError> {
    for path in toolkit.triplet() {
        let relative = path.strip_prefix(&toolkit.root).unwrap_or(path.as_path());
        require_nonempty(&path, format!("missing or empty: {}", relative.display()))?;
    }
    require_exact(&toolkit.gm2p(), GM2P_BYTES, "boot.efi.gm2p")?;
    require_exact(&toolkit.tzmap(), TZMAP_BYTES, "boot.efi.tzmap")
}

fn verify_tzmap(toolkit: &Toolkit) -> Result<(), CanoeError> {
    if !toolkit.abl_original().is_file() {
        note("skipping ABL/tzmap consistency check: ABL_original.efi is unavailable");
        return Ok(());
    }
    canoe_bootmgr::verify_tzmap(
        Some(&toolkit.bin()),
        &toolkit.tzmap(),
        &toolkit.abl_original(),
        true,
    )
    .map_err(|error| CanoeError::message(format!("abl_tzmap verify failed: {error}")))
}

fn detect_export() -> Result<Option<PathBuf>, canoe_bootmgr::fastboot::FastbootError> {
    let candidates = canoe_bootmgr::detect_sources().map_err(|error| {
        canoe_bootmgr::fastboot::FastbootError::Discovery {
            message: error.to_string(),
        }
    })?;
    Ok(candidates
        .iter()
        .find(|candidate| canoe_bootmgr::detect::is_export_candidate(candidate))
        .map(|candidate| candidate.path.clone()))

}
fn install_to_backend(
    toolkit: &Toolkit,
    staging: &Path,
    options: &InstallOptions,
) -> Result<(String, canoe_bootmgr::InstallReceipt), CanoeError> {
    let slot = match options.slot.as_str() {
        "a" => canoe_bootmgr::Slot::A,
        "b" => canoe_bootmgr::Slot::B,
        _ => return Err(CanoeError::message("slot must be a or b")),
    };
    let request = canoe_bootmgr::InstallRequest {
        staged: staging.to_path_buf(),
        slot,
        mode: options.mode,
        allow_new_signer: options.allow_new_signer,
};
    if let Some(path) = options.boot_root.as_deref() {
        let root = local_boot_root(path)?;
        let backend = canoe_bootmgr::Backend::local(&root)
            .map_err(|error| CanoeError::message(error.to_string()))?;
        step("Installing the staged boot root");
        let receipt = canoe_bootmgr::install(&backend, &request)
            .map_err(|error| CanoeError::message(error.to_string()))?;
        return Ok((root.display().to_string(), receipt));
    }
    let fastboot = canoe_bootmgr::fastboot::binary(Some(&toolkit.root))
        .map_err(|error| CanoeError::message(error.to_string()))?;
    let exported = canoe_bootmgr::fastboot::export(
        &fastboot,
        "persist",
        Duration::from_secs(60),
        detect_export,
    )
    .map_err(|error| CanoeError::message(error.to_string()))?;
    if exported.adopted {
        note(&format!(
            "Adopting the mass-storage export already live at {}",
            exported.node.display()
        ));
}
    let helper = toolkit.tool("canoe-ext4")?;
    let backend = canoe_bootmgr::Backend::ext4_with_helper(&exported.node, &helper)
        .map_err(|error| CanoeError::message(error.to_string()))?;
    step("Installing the staged boot root");
    let receipt = canoe_bootmgr::install(&backend, &request)
        .map_err(|error| CanoeError::message(error.to_string()))?;
    Ok((exported.node.display().to_string(), receipt))

}
pub fn run(args: &[String]) -> Result<(), CanoeError> {
    let options = parse(args)?;
    let toolkit = Toolkit::shipped();
    validate_triplet(&toolkit)?;
    verify_tzmap(&toolkit)?;
    let staging = TempDir::create()?;
    stage_files(&toolkit, staging.path())?;
    let vendor_output = if let Some(input) = options.vendor_boot.as_deref() {
        let output = toolkit.root.join("work/vendor_boot_patched.img");
        fs::create_dir_all(toolkit.root.join("work"))?;
        canoe_bootmgr::vendor_boot_patch(input, &output)
            .map_err(|error| CanoeError::message(error.to_string()))?;
        Some(output)
    } else {
        None
    };
    if options.boot_root.is_none() {
        match canoe_bootmgr::fastboot::binary(Some(&toolkit.root)) {
            Ok(fastboot) => {
                let identity = canoe_bootmgr::fastboot::identify(&fastboot, Duration::from_secs(10));
                if identity.bds_version.is_none() {
                    warn("The device does not look like Super Fastboot; fastboot oem mass-storage:persist does not exist outside the BDS.");
                }
            }
            Err(error) => warn(&format!("Could not identify the device with fastboot: {error}")),
        }
        step("Exporting persist over USB Mass Storage");
    }
    let (destination, receipt) = install_to_backend(&toolkit, staging.path(), &options)?;
    emit(&stage_report(&destination, options.mode, !receipt.backup_present, vendor_output.as_deref()));
    Ok(())
}
