use std::collections::HashMap;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::config::{ConfigDocument, ConfigEntry, Role};
use crate::slots::{self, GM2P_BYTES, Slot, SlotError, TZMAP_BYTES};
use serde::Serialize;
use sha2::{Digest, Sha256};

const SIDECARS: [&str; 3] = ["", ".gm2p", ".tzmap"];
const SIGNER_START: usize = 0x38;
const SIGNER_END: usize = 0x58;

#[derive(Debug, Clone)]
pub struct InstallInput {
    pub staged: PathBuf,
    pub target: Slot,
    pub both: bool,
    pub active: Option<Slot>,
    pub mode: Option<u8>,
    pub allow_new_signer: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct InstallReceipt {
    pub active_slot: Slot,
    pub installed: Vec<Slot>,
    pub generation: u32,
    pub signer_changed: bool,
    pub backup_present: bool,
}

pub fn install(root: &Path, input: &InstallInput) -> Result<InstallReceipt, SlotError> {
    validate_staged(&input.staged)?;
    let active = input.active.unwrap_or(input.target);
    let installed = if input.both {
        vec![input.target.other(), input.target]
    } else {
        vec![input.target]
    };
    let tools = crate::slot_tools::staged(&input.staged)?;
    let snapshot = snapshot(root, &tools)?;
    let mut moved = Vec::new();
    let result = install_inner(root, input, active, &installed, &tools, &mut moved);
    match result {
        Ok(receipt) => Ok(receipt),
        Err(error) => {
            for path in moved {
                let _ = fs::remove_file(path);
            }
            restore_snapshot(&snapshot);
            Err(error)
        }
    }
}

fn install_inner(
    root: &Path,
    input: &InstallInput,
    active: Slot,
    installed: &[Slot],
    tools: &[PathBuf],
    moved: &mut Vec<PathBuf>,
) -> Result<InstallReceipt, SlotError> {
    for slot in installed {
        signer_gate(root, *slot, &input.staged, input.allow_new_signer)?;
    }
    migrate_legacy(root, input.target, moved)?;
    quarantine_orphans(root, moved)?;
    let mut changed_signer = false;
    for slot in installed {
        changed_signer |= signer_changed(root, *slot, &input.staged);
        demote(root, *slot)?;
        commit_slot(root, *slot, &input.staged)?;
    }
    crate::slot_tools::commit(root, tools)?;
    let mut config = read_config(root)?;
    let rows = managed_rows(root, &mut config, active, input.mode)?;
    let generation = config
        .sync_managed_rows(&rows)
        .map_err(|error| SlotError::Invalid(error.to_string()))?;
    write_config(root, &config)?;
    stamp(root)?;
    Ok(InstallReceipt {
        active_slot: active,
        installed: installed.to_vec(),
        generation,
        signer_changed: changed_signer,
        backup_present: slots::valid_backup(root)?,
    })
}

fn validate_staged(staged: &Path) -> Result<(), SlotError> {
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
    let mut destination = directory.join(
        source
            .file_name()
            .ok_or_else(|| SlotError::Invalid("quarantine source has no name".to_owned()))?,
    );
    let mut suffix = 0_u32;
    while destination.exists() {
        suffix += 1;
        destination = directory.join(format!(
            "{}.{}",
            source.file_name().unwrap_or_default().to_string_lossy(),
            suffix
        ));
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

fn managed_rows(
    root: &Path,
    config: &mut ConfigDocument,
    active: Slot,
    mode: Option<u8>,
) -> Result<Vec<ConfigEntry>, SlotError> {
    let effective_mode = mode.unwrap_or(config.mode);
    let mut rows = Vec::new();
    for slot in [Slot::A, Slot::B] {
        if slots::valid_triplet(root, slot)? {
            rows.push(ConfigEntry {
                id: slot.row_id().to_owned(),
                title: format!("Android {}", slot.suffix().to_ascii_uppercase()),
                image: slot.loader_name().to_owned(),
                options: None,
                mode: effective_mode,
                role: if slot == active {
                    Role::Active
                } else {
                    Role::Inactive
                },
                unknown: Vec::new(),
            });
        }
    }
    if slots::valid_backup(root)? {
        rows.push(ConfigEntry {
            id: "android-backup".to_owned(),
            title: "Android (previous)".to_owned(),
            image: "boot_backup.efi".to_owned(),
            options: None,
            mode: effective_mode,
            role: Role::Backup,
            unknown: Vec::new(),
        });
    }
    if rows.is_empty() {
        return Err(SlotError::Invalid(
            "install produced no valid slot triplet".to_owned(),
        ));
    }
    Ok(rows)
}

fn read_config(root: &Path) -> Result<ConfigDocument, SlotError> {
    match fs::read(root.join("canoe.cfg")) {
        Ok(bytes) => {
            ConfigDocument::parse(&bytes).map_err(|error| SlotError::Invalid(error.to_string()))
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(ConfigDocument::empty()),
        Err(error) => Err(io("read config", &root.join("canoe.cfg"), error)),
    }
}

fn write_config(root: &Path, config: &ConfigDocument) -> Result<(), SlotError> {
    let path = root.join("canoe.cfg");
    write_atomic(
        &path,
        &config
            .serialize()
            .map_err(|error| SlotError::Invalid(error.to_string()))?,
    )
}

fn stamp(root: &Path) -> Result<(), SlotError> {
    let mut parts = Vec::new();
    for path in slots::triplet_paths(root, Slot::A)
        .into_iter()
        .chain(slots::triplet_paths(root, Slot::B))
        .chain(slots::backup_paths(root))
    {
        if path.is_file() {
            parts.push(hex_digest(&path)?);
        }
    }
    write_atomic(
        &root.join(".canoe.gen"),
        format!("CANOEG1|-|{}\n", parts.join("|")).as_bytes(),
    )
}

fn hex_digest(path: &Path) -> Result<String, SlotError> {
    let bytes = fs::read(path).map_err(|error| io("read generation input", path, error))?;
    Ok(Sha256::digest(bytes)
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect())
}

pub(crate) fn copy_file(source: &Path, destination: &Path) -> Result<(), SlotError> {
    let bytes = fs::read(source).map_err(|error| io("read", source, error))?;
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent).map_err(|error| io("create directory", parent, error))?;
    }
    let mut file = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(destination)
        .map_err(|error| io("open", destination, error))?;
    file.write_all(&bytes)
        .map_err(|error| io("write", destination, error))?;
    file.sync_all()
        .map_err(|error| io("sync", destination, error))?;
    Ok(())
}

fn write_atomic(path: &Path, bytes: &[u8]) -> Result<(), SlotError> {
    let temporary = path.with_extension("tmp.canoe");
    copy_bytes(&temporary, bytes)?;
    fs::rename(&temporary, path).map_err(|error| io("replace", path, error))
}

fn copy_bytes(path: &Path, bytes: &[u8]) -> Result<(), SlotError> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|error| io("create directory", parent, error))?;
    }
    let mut file = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(path)
        .map_err(|error| io("open", path, error))?;
    file.write_all(bytes)
        .map_err(|error| io("write", path, error))?;
    file.sync_all().map_err(|error| io("sync", path, error))
}

fn remove_if_present(path: &Path) -> Result<(), SlotError> {
    match fs::remove_file(path) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(io("remove", path, error)),
    }
}

fn snapshot(
    root: &Path,
    tools: &[PathBuf],
) -> Result<HashMap<PathBuf, Option<Vec<u8>>>, SlotError> {
    let mut all = Vec::new();
    for slot in [Slot::A, Slot::B] {
        all.extend(slots::triplet_paths(root, slot));
    }
    all.extend(slots::backup_paths(root));
    all.extend(slots::legacy_paths(root));
    all.extend([root.join("canoe.cfg"), root.join(".canoe.gen")]);
    all.extend(crate::slot_tools::destinations(root, tools));
    all.into_iter()
        .map(|path| {
            let value = match fs::read(&path) {
                Ok(bytes) => Some(bytes),
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
                Err(error) => return Err(io("snapshot", &path, error)),
            };
            Ok((path, value))
        })
        .collect()
}
fn restore_snapshot(snapshot: &HashMap<PathBuf, Option<Vec<u8>>>) {
    for (path, value) in snapshot {
        match value {
            Some(bytes) => {
                let _ = copy_bytes(path, bytes);
            }
            None => {
                let _ = fs::remove_file(path);
            }
        }
    }
}

pub(crate) fn io(operation: &'static str, path: &Path, source: std::io::Error) -> SlotError {
    SlotError::Io {
        slot: "root".to_owned(),
        operation,
        path: path.to_owned(),
        source,
    }
}
