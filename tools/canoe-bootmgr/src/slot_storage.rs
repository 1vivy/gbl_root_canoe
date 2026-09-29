use std::collections::HashMap;
use std::fs::{self, OpenOptions};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use crate::config::ConfigDocument;
use crate::slots::{self, Slot, SlotError};
use sha2::{Digest, Sha256};

#[derive(Debug)]
pub(crate) struct InstallSnapshot {
    root: PathBuf,
    files: HashMap<PathBuf, Option<Vec<u8>>>,
    had_tools_directory: bool,
    had_quarantine_directory: bool,
}

pub(crate) fn write_config(root: &Path, config: &ConfigDocument) -> Result<(), SlotError> {
    let path = root.join("canoe.cfg");
    write_atomic(
        root,
        &path,
        &config
            .serialize()
            .map_err(|error| SlotError::Invalid(error.to_string()))?,
    )
}

pub(crate) fn stamp(root: &Path) -> Result<(), SlotError> {
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
        root,
        &root.join(".canoe.gen"),
        format!("CANOEG1|-|{}\n", parts.join("|")).as_bytes(),
    )
}

pub(crate) fn copy_file(source: &Path, destination: &Path) -> Result<(), SlotError> {
    let bytes = fs::read(source).map_err(|error| io("read", source, error))?;
    write_regular(destination, &bytes)
}

pub(crate) fn write_regular(destination: &Path, bytes: &[u8]) -> Result<(), SlotError> {
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent).map_err(|error| io("create directory", parent, error))?;
    }
    let present = match fs::symlink_metadata(destination) {
        Ok(metadata) if metadata.is_file() => true,
        Ok(_) => {
            return Err(SlotError::Invalid(format!(
                "destination is not a regular file: {}",
                destination.display()
            )));
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => false,
        Err(error) => return Err(io("stat destination", destination, error)),
    };
    let mut options = OpenOptions::new();
    options.write(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;

        options.custom_flags(libc::O_NOFOLLOW);
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        use windows_sys::Win32::Storage::FileSystem::FILE_FLAG_OPEN_REPARSE_POINT;

        options.custom_flags(FILE_FLAG_OPEN_REPARSE_POINT);
    }
    if present {
        options.create(false);
    } else {
        options.create_new(true);
    }
    let mut file = options
        .open(destination)
        .map_err(|error| io("open", destination, error))?;
    if !file
        .metadata()
        .map_err(|error| io("stat opened destination", destination, error))?
        .is_file()
    {
        return Err(SlotError::Invalid(format!(
            "destination is not a regular file: {}",
            destination.display()
        )));
    }
    file.set_len(0)
        .map_err(|error| io("truncate", destination, error))?;
    file.write_all(bytes)
        .map_err(|error| io("write", destination, error))?;
    file.sync_all()
        .map_err(|error| io("sync", destination, error))
}

pub(crate) fn remove_if_present(path: &Path) -> Result<(), SlotError> {
    match fs::remove_file(path) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(io("remove", path, error)),
    }
}

pub(crate) fn snapshot(root: &Path, tools: &[PathBuf]) -> Result<InstallSnapshot, SlotError> {
    let mut all = Vec::new();
    for slot in [Slot::A, Slot::B] {
        all.extend(slots::triplet_paths(root, slot));
    }
    all.extend(slots::backup_paths(root));
    all.extend(slots::legacy_paths(root));
    all.extend([root.join("canoe.cfg"), root.join(".canoe.gen")]);
    all.extend(crate::slot_tools::destinations(root, tools));
    let files = all
        .into_iter()
        .map(|path| {
            let value = match crate::file_identity::open_readonly(&path) {
                Ok(mut file) => {
                    let mut bytes = Vec::new();
                    file.read_to_end(&mut bytes)
                        .map_err(|error| io("snapshot", &path, error))?;
                    Some(bytes)
                }
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
                Err(error) => return Err(io("snapshot", &path, error)),
            };
            Ok((path, value))
        })
        .collect::<Result<HashMap<_, _>, SlotError>>()?;
    Ok(InstallSnapshot {
        root: root.to_owned(),
        files,
        had_tools_directory: root.join("tools").is_dir(),
        had_quarantine_directory: root.join(".canoe-quarantine").is_dir(),
    })
}

pub(crate) fn restore_snapshot(
    snapshot: &InstallSnapshot,
    moved: &[PathBuf],
) -> Result<(), SlotError> {
    let mut first_error = None;
    for path in moved {
        if let Err(error) = remove_if_present(path) {
            first_error.get_or_insert(error);
        }
    }
    for (path, value) in &snapshot.files {
        let result = match value {
            Some(bytes) => write_regular(path, bytes),
            None => remove_if_present(path),
        };
        if let Err(error) = result {
            first_error.get_or_insert(error);
        }
    }
    for (directory, existed) in [
        ("tools", snapshot.had_tools_directory),
        (".canoe-quarantine", snapshot.had_quarantine_directory),
    ] {
        if !existed {
            let path = snapshot.root.join(directory);
            if let Err(error) = remove_created_directory(&path) {
                first_error.get_or_insert(error);
            }
        }
    }
    match first_error {
        Some(error) => Err(error),
        None => Ok(()),
    }
}

fn hex_digest(path: &Path) -> Result<String, SlotError> {
    let bytes = fs::read(path).map_err(|error| io("read generation input", path, error))?;
    Ok(Sha256::digest(bytes)
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect())
}

fn write_atomic(root: &Path, path: &Path, bytes: &[u8]) -> Result<(), SlotError> {
    crate::backend::atomic_replace(root, path, bytes).map_err(|error| match error {
        crate::backend::BackendError::Io {
            operation,
            path,
            source,
        } => SlotError::Io {
            slot: "root".to_owned(),
            operation,
            path,
            source,
        },
        error => SlotError::Invalid(error.to_string()),
    })
}

fn remove_created_directory(path: &Path) -> Result<(), SlotError> {
    match fs::remove_dir(path) {
        Ok(()) => Ok(()),
        Err(error)
            if matches!(
                error.kind(),
                std::io::ErrorKind::NotFound | std::io::ErrorKind::DirectoryNotEmpty
            ) =>
        {
            Ok(())
        }
        Err(error) => Err(io("remove created directory", path, error)),
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

#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::PathBuf;

    use super::{restore_snapshot, snapshot};

    #[test]
    fn rollback_removes_tools_and_quarantine_directories_created_by_transaction() {
        // Given a snapshot from before the transaction creates its managed directories.
        let root = tempfile::tempdir().expect("boot root");
        let tool = PathBuf::from("UsbTools.efi");
        let snapshot = snapshot(root.path(), &[tool]).expect("snapshot");
        let tools = root.path().join("tools");
        let quarantine = root.path().join(".canoe-quarantine");
        fs::create_dir(&tools).expect("create tools");
        fs::write(tools.join("UsbTools.efi"), b"tool").expect("write tool");
        fs::create_dir(&quarantine).expect("create quarantine");
        let moved = quarantine.join("orphaned.efi");
        fs::write(&moved, b"orphan").expect("write quarantine file");

        // When the transaction rolls back its file changes.
        restore_snapshot(&snapshot, &[moved]).expect("rollback");

        // Then neither directory remains after its newly created contents are removed.
        assert!(!tools.exists());
        assert!(!quarantine.exists());
    }
}
