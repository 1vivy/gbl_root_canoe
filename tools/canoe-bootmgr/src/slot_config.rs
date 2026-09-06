use std::fs;
use std::path::Path;

use crate::config::{ConfigDocument, ConfigEntry, Role};
use crate::slot_storage::io;
use crate::slots::{self, Slot, SlotError};

pub(crate) fn managed_rows(
    root: &Path,
    config: &mut ConfigDocument,
    active: Slot,
    mode: Option<u8>,
) -> Result<Vec<ConfigEntry>, SlotError> {
    let effective_mode = mode.unwrap_or(config.mode);
    config.mode = effective_mode;
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

pub(crate) fn read_config(root: &Path) -> Result<ConfigDocument, SlotError> {
    match fs::read(root.join("canoe.cfg")) {
        Ok(bytes) => {
            ConfigDocument::parse(&bytes).map_err(|error| SlotError::Invalid(error.to_string()))
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(ConfigDocument::empty()),
        Err(error) => Err(io("read config", &root.join("canoe.cfg"), error)),
    }
}
