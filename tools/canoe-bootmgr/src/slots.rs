use std::fs;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use thiserror::Error;

pub const GM2P_BYTES: usize = 120;
pub const TZMAP_BYTES: usize = 256;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Slot {
    A,
    B,
}

impl Slot {
    #[must_use]
    pub const fn suffix(self) -> &'static str {
        match self {
            Self::A => "a",
            Self::B => "b",
        }
    }

    #[must_use]
    pub const fn other(self) -> Self {
        match self {
            Self::A => Self::B,
            Self::B => Self::A,
        }
    }

    #[must_use]
    pub const fn row_id(self) -> &'static str {
        match self {
            Self::A => "android-a",
            Self::B => "android-b",
        }
    }

    #[must_use]
    pub const fn loader_name(self) -> &'static str {
        match self {
            Self::A => "boot_a.efi",
            Self::B => "boot_b.efi",
        }
    }
}

impl std::fmt::Display for Slot {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(self.suffix())
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct SlotStatus {
    pub active_slot: Option<Slot>,
    pub source: String,
    pub inactive_slot: Option<Slot>,
}

#[derive(Debug, Error)]
pub enum SlotError {
    #[error("slot: {0}")]
    Invalid(String),
    #[error("slot {slot}: {operation} {path}: {source}")]
    Io {
        slot: String,
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
}

pub fn parse_slot(value: &str) -> Result<Slot, SlotError> {
    match value
        .trim()
        .trim_start_matches('_')
        .to_ascii_lowercase()
        .as_str()
    {
        "a" | "0" => Ok(Slot::A),
        "b" | "1" => Ok(Slot::B),
        other => Err(SlotError::Invalid(format!(
            "slot must be a or b, got {other:?}"
        ))),
    }
}

pub fn resolve_active(
    explicit: Option<Slot>,
    bootctl_output: Option<&str>,
    gpt_active: Option<Slot>,
) -> SlotStatus {
    let (active_slot, source) = if let Some(slot) = explicit {
        (Some(slot), "explicit")
    } else if let Some(slot) = bootctl_output.and_then(parse_bootctl_output) {
        (Some(slot), "bootctl")
    } else if let Some(slot) = gpt_active {
        (Some(slot), "gpt")
    } else {
        (None, "unknown")
    };
    SlotStatus {
        inactive_slot: active_slot.map(Slot::other),
        active_slot,
        source: source.to_owned(),
    }
}

fn parse_bootctl_output(output: &str) -> Option<Slot> {
    let lower = output.to_ascii_lowercase();
    for line in lower.lines() {
        let trimmed = line.trim();
        if trimmed.contains("current-slot") || trimmed.contains("active-slot") {
            if let Some(value) = trimmed.split([':', '=', ' ', '\t']).next_back() {
                if let Ok(slot) = parse_slot(value) {
                    return Some(slot);
                }
            }
        }
    }
    for token in
        lower.split(|character: char| !character.is_ascii_alphanumeric() && character != '_')
    {
        if let Ok(slot) = parse_slot(token) {
            return Some(slot);
        }
    }
    None
}

#[must_use]
pub fn triplet_paths(root: &Path, slot: Slot) -> [PathBuf; 3] {
    let loader = root.join(slot.loader_name());
    [
        loader.clone(),
        loader.with_extension("efi.gm2p"),
        loader.with_extension("efi.tzmap"),
    ]
}

#[must_use]
pub fn backup_paths(root: &Path) -> [PathBuf; 3] {
    let loader = root.join("boot_backup.efi");
    [
        loader.clone(),
        loader.with_extension("efi.gm2p"),
        loader.with_extension("efi.tzmap"),
    ]
}

#[must_use]
pub fn legacy_paths(root: &Path) -> [PathBuf; 3] {
    let loader = root.join("boot.efi");
    [
        loader.clone(),
        loader.with_extension("efi.gm2p"),
        loader.with_extension("efi.tzmap"),
    ]
}

pub fn valid_triplet(root: &Path, slot: Slot) -> Result<bool, SlotError> {
    valid_paths(&triplet_paths(root, slot), slot)
}

pub fn valid_backup(root: &Path) -> Result<bool, SlotError> {
    valid_paths(&backup_paths(root), Slot::A)
}

fn valid_paths(paths: &[PathBuf; 3], slot: Slot) -> Result<bool, SlotError> {
    let loader = match fs::metadata(&paths[0]) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(false),
        Err(source) => {
            return Err(SlotError::Io {
                slot: slot.to_string(),
                operation: "stat",
                path: paths[0].clone(),
                source,
            });
        }
    };
    if !loader.is_file() || loader.len() == 0 {
        return Ok(false);
    }
    for (path, expected) in paths.iter().skip(1).zip([GM2P_BYTES, TZMAP_BYTES]) {
        match fs::metadata(path) {
            Ok(metadata) if metadata.is_file() && metadata.len() == expected as u64 => {}
            Ok(_) => return Ok(false),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(false),
            Err(source) => {
                return Err(SlotError::Io {
                    slot: slot.to_string(),
                    operation: "stat",
                    path: path.clone(),
                    source,
                });
            }
        }
    }
    Ok(true)
}
