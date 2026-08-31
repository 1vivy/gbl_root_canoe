//! Carriage of the standalone EFI tools that ship beside a managed loader.
//!
//! The BDS launches `<boot root>/tools/*.efi` from its tools submenu. They are
//! not per-slot and not part of the loader triplet, so they are committed once
//! per install rather than demoted with a slot. A host toolkit stages them next
//! to the triplet; a device installer usually stages none, and an absent
//! directory is not an error.

use std::fs;
use std::path::{Path, PathBuf};

use crate::slots::SlotError;

/// Staged tool files, sorted by name so an install is reproducible.
pub(crate) fn staged(staged: &Path) -> Result<Vec<PathBuf>, SlotError> {
    let directory = staged.join("tools");
    if !directory.exists() {
        return Ok(Vec::new());
    }
    if !directory.is_dir() {
        return Err(SlotError::Invalid(format!(
            "staged tools path is not a directory: {}",
            directory.display()
        )));
    }
    let entries = fs::read_dir(&directory)
        .map_err(|error| crate::slot_transaction::io("read directory", &directory, error))?;
    let mut files = Vec::new();
    for entry in entries {
        let entry =
            entry.map_err(|error| crate::slot_transaction::io("read entry", &directory, error))?;
        let path = entry.path();
        if path.is_file() {
            files.push(path);
        }
    }
    files.sort();
    Ok(files)
}

/// Where each staged tool lands inside the boot root.
pub(crate) fn destinations(root: &Path, sources: &[PathBuf]) -> Vec<PathBuf> {
    let directory = root.join("tools");
    sources
        .iter()
        .filter_map(|source| source.file_name())
        .map(|name| directory.join(name))
        .collect()
}

/// Copy every staged tool into the boot root, replacing an older generation.
pub(crate) fn commit(root: &Path, sources: &[PathBuf]) -> Result<(), SlotError> {
    for (source, destination) in sources.iter().zip(destinations(root, sources)) {
        crate::slot_transaction::copy_file(source, &destination)?;
    }
    Ok(())
}
