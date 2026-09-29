use std::fs;
use std::path::{Path, PathBuf};

use crate::slot_config::{managed_rows, read_config};
use crate::slot_storage::{copy_file, io, remove_if_present, stamp, write_config};
use crate::slot_transaction::{InstallInput, InstallReceipt};
use crate::slots::{self, GM2P_BYTES, Slot, SlotError, TZMAP_BYTES};

const SIDECARS: [&str; 3] = ["", ".gm2p", ".tzmap"];
const SIGNER_START: usize = 0x38;
const SIGNER_END: usize = 0x58;

pub(crate) fn install_inner(
    root: &Path,
    input: &InstallInput,
    staged: &crate::staged_input::StagedInput,
    active: Slot,
    installed: &[Slot],
    moved: &mut Vec<PathBuf>,
) -> Result<InstallReceipt, SlotError> {
    for slot in installed {
        signer_gate(root, *slot, &staged.root, input.allow_new_signer)?;
    }
    migrate_legacy(root, input.target, moved)?;
    quarantine_orphans(root, moved)?;
    let mut changed_signer = false;
    for slot in installed {
        changed_signer |= signer_changed(root, *slot, &staged.root);
        demote(root, *slot)?;
        commit_slot(root, *slot, &staged.root)?;
    }
    crate::slot_tools::commit(root, &staged.tools)?;
    let mut config = read_config(root)?;
    let rows = managed_rows(root, &mut config, active, input.mode)?;
    let generation = config
        .sync_managed_rows(&rows, Some(input.target.row_id()))
        .map_err(|error| SlotError::Invalid(error.to_string()))?;
    write_config(root, &config)?;
    stamp(root)?;
    Ok(InstallReceipt {
        active_slot: active,
        installed: installed.to_vec(),
        generation,
        signer_changed: changed_signer,
        backup_present: slots::valid_backup(root)?,
        staged: input.staged.clone(),
        loader_bytes: staged.loader.bytes,
        loader_sha256: staged.loader.sha256.clone(),
        gm2p_bytes: staged.gm2p.bytes,
        gm2p_sha256: staged.gm2p.sha256.clone(),
        tzmap_bytes: staged.tzmap.bytes,
        tzmap_sha256: staged.tzmap.sha256.clone(),
        tools: staged.tool_inventory.clone(),
        mode_request: input.mode_request.clone(),
        acknowledged: Vec::new(),
        warnings: Vec::new(),
    })
}

pub(crate) fn validate_staged(staged: &Path) -> Result<(), SlotError> {
    if !staged.is_dir() {
        return Err(SlotError::Invalid(format!(
            "staged path is not a directory: {}",
            staged.display()
        )));
    }
    let loader = staged.join("boot.efi");
    let gm2p = staged.join("boot.efi.gm2p");
    let tzmap = staged.join("boot.efi.tzmap");
    if !loader.is_file()
        || loader
            .metadata()
            .map_err(|error| io("stat staged loader", &loader, error))?
            .len()
            == 0
    {
        return Err(SlotError::Invalid(
            "staged boot.efi is missing or empty".to_owned(),
        ));
    }
    require_size(&gm2p, GM2P_BYTES)?;
    mode2_profile::validate_file(&gm2p)
        .map_err(|error| SlotError::Invalid(format!("invalid staged GM2P profile: {error}")))?;
    require_size(&tzmap, TZMAP_BYTES)
}

fn require_size(path: &Path, expected: usize) -> Result<(), SlotError> {
    let size = path
        .metadata()
        .map_err(|error| io("stat staged sidecar", path, error))?
        .len();
    if size != expected as u64 {
        return Err(SlotError::Invalid(format!(
            "{} must be exactly {expected} bytes",
            path.display()
        )));
    }
    Ok(())
}

fn signer_gate(root: &Path, slot: Slot, staged: &Path, allow: bool) -> Result<(), SlotError> {
    let existing = slots::triplet_paths(root, slot)[1].clone();
    let current = match fs::read(&existing) {
        Ok(bytes) if bytes.len() >= SIGNER_END => Some(bytes[SIGNER_START..SIGNER_END].to_vec()),
        Ok(_) => None,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
        Err(error) => return Err(io("read installed signer", &existing, error)),
    };
    let staged_bytes = fs::read(staged.join("boot.efi.gm2p"))
        .map_err(|error| io("read staged signer", &staged.join("boot.efi.gm2p"), error))?;
    let changed = current.is_some_and(|value| value != staged_bytes[SIGNER_START..SIGNER_END]);
    if changed && !allow {
        return Err(SlotError::Invalid(
            "vbmeta signer changed; pass --allow-new-signer to continue".to_owned(),
        ));
    }
    Ok(())
}

fn signer_changed(root: &Path, slot: Slot, staged: &Path) -> bool {
    let existing = slots::triplet_paths(root, slot)[1].clone();
    let Ok(old) = fs::read(existing) else {
        return false;
    };
    let Ok(new) = fs::read(staged.join("boot.efi.gm2p")) else {
        return false;
    };
    old.len() >= SIGNER_END
        && new.len() >= SIGNER_END
        && old[SIGNER_START..SIGNER_END] != new[SIGNER_START..SIGNER_END]
}

fn migrate_legacy(root: &Path, target: Slot, moved: &mut Vec<PathBuf>) -> Result<(), SlotError> {
    let legacy = slots::legacy_paths(root);
    let valid = legacy[0].is_file()
        && legacy[0]
            .metadata()
            .map_err(|error| io("stat legacy loader", &legacy[0], error))?
            .len()
            > 0
        && legacy[1].is_file()
        && legacy[1]
            .metadata()
            .map_err(|error| io("stat legacy gm2p", &legacy[1], error))?
            .len()
            == GM2P_BYTES as u64
        && legacy[2].is_file()
        && legacy[2]
            .metadata()
            .map_err(|error| io("stat legacy tzmap", &legacy[2], error))?
            .len()
            == TZMAP_BYTES as u64;
    if valid && !slots::valid_triplet(root, target)? {
        let target_paths = slots::triplet_paths(root, target);
        for (source, destination) in legacy.iter().zip(target_paths) {
            copy_file(source, &destination)?;
        }
    }
    for source in legacy {
        if source.is_file() {
            if valid {
                fs::remove_file(&source)
                    .map_err(|error| io("remove migrated legacy", &source, error))?;
            } else {
                quarantine(&source, root, moved)?;
            }
        }
    }
    Ok(())
}

fn quarantine_orphans(root: &Path, moved: &mut Vec<PathBuf>) -> Result<(), SlotError> {
    for slot in [Slot::A, Slot::B] {
        if slots::valid_triplet(root, slot)? {
            continue;
        }
        for path in slots::triplet_paths(root, slot) {
            if path.is_file() {
                quarantine(&path, root, moved)?;
            }
        }
    }
    Ok(())
}

fn quarantine(source: &Path, root: &Path, moved: &mut Vec<PathBuf>) -> Result<(), SlotError> {
    let directory = root.join(".canoe-quarantine");
    fs::create_dir_all(&directory).map_err(|error| io("create quarantine", &directory, error))?;
    let name = source
        .file_name()
        .ok_or_else(|| SlotError::Invalid("quarantine source has no name".to_owned()))?;
    let mut destination = directory.join(name);
    let mut suffix = 0_u32;
    while destination.exists() {
        suffix += 1;
        destination = directory.join(format!("{}.{}", name.to_string_lossy(), suffix));
    }
    fs::rename(source, &destination).map_err(|error| io("quarantine file", source, error))?;
    moved.push(destination);
    Ok(())
}

fn demote(root: &Path, slot: Slot) -> Result<(), SlotError> {
    let source = slots::triplet_paths(root, slot);
    let target = slots::backup_paths(root);
    if slots::valid_triplet(root, slot)? {
        for (source, target) in source.into_iter().zip(target) {
            copy_file(&source, &target)?;
        }
    } else {
        for target in target {
            remove_if_present(&target)?;
        }
    }
    Ok(())
}

fn commit_slot(root: &Path, slot: Slot, staged: &Path) -> Result<(), SlotError> {
    let target = slots::triplet_paths(root, slot);
    for (suffix, destination) in SIDECARS.into_iter().zip(target) {
        copy_file(&staged.join(format!("boot.efi{suffix}")), &destination)?;
    }
    Ok(())
}
