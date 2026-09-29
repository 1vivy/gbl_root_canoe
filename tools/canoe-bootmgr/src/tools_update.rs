use std::collections::HashMap;
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};

use thiserror::Error;

use crate::file_identity::FileIdentity;
use crate::slot_tools;

#[derive(Debug, Error)]
pub enum ToolsUpdateError {
    #[error("tools source does not exist: {source_path}")]
    SourceMissing { source_path: PathBuf },
    #[error("tools source is not a directory: {source_path}")]
    SourceNotDirectory { source_path: PathBuf },
    #[error("tools source contains no regular files: {source_path}")]
    SourceEmpty { source_path: PathBuf },
    #[error("tools source file name is not UTF-8: {path}")]
    SourceName { path: PathBuf },
    #[error("tools snapshot {path}: {message}")]
    Snapshot { path: PathBuf, message: String },
    #[error("tools write: {message}")]
    Write { message: String },
    #[error("tools rollback {path}: {message}")]
    Rollback { path: PathBuf, message: String },
}

impl ToolsUpdateError {
    pub(crate) fn protocol_code(&self) -> &'static str {
        match self {
            Self::SourceMissing { .. } => "tools-source-missing",
            Self::SourceNotDirectory { .. } => "tools-source-not-directory",
            Self::SourceEmpty { .. } => "tools-source-empty",
            Self::SourceName { .. } => "tools-source-name",
            Self::Snapshot { .. } => "tools-snapshot",
            Self::Write { .. } => "tools-write",
            Self::Rollback { .. } => "tools-rollback",
        }
    }
}
#[derive(Debug, Clone, serde::Serialize)]
pub(crate) struct ToolsUpdateReceipt {
    pub files: Vec<String>,
    pub inventory: Vec<FileIdentity>,
}

pub(crate) fn update_with_inventory(
    root: &Path,
    source: &Path,
    expected_inventory: &[FileIdentity],
) -> Result<ToolsUpdateReceipt, ToolsUpdateError> {
    let sources = crate::tools_inventory::source_files(source)?;
    let mut inventory = Vec::with_capacity(sources.len());
    let staged =
        crate::build_tools::WorkDir::new().map_err(|error| ToolsUpdateError::Snapshot {
            path: source.to_owned(),
            message: error.to_string(),
        })?;
    let staged_tools = staged.path().join("tools");
    fs::create_dir_all(&staged_tools).map_err(|error| ToolsUpdateError::Snapshot {
        path: staged_tools.clone(),
        message: error.to_string(),
    })?;
    for path in &sources {
        let name = path
            .file_name()
            .and_then(|name| name.to_str())
            .ok_or_else(|| ToolsUpdateError::SourceName { path: path.clone() })?;
        let expected = crate::tools_inventory::expected_for_name(
            expected_inventory,
            std::ffi::OsStr::new(name),
        )
        .map_err(|message| ToolsUpdateError::Write { message })?;
        let expected = match (expected_inventory.is_empty(), expected) {
            (true, _) => None,
            (false, Some(expected)) => Some(expected),
            (false, None) => {
                return Err(ToolsUpdateError::Write {
                    message: format!("tools inventory is missing {}", path.display()),
                });
            }
        };
        let destination = staged_tools.join(name);
        let staged_identity = crate::file_identity::stage(
            path,
            &destination,
            expected.map(|entry| entry.bytes),
            expected.map(|entry| entry.sha256.as_str()),
        )
        .map_err(|error| ToolsUpdateError::Write {
            message: format!("tools inventory changed {}: {error}", path.display()),
        })?;
        inventory.push(FileIdentity {
            path: path.clone(),
            ..staged_identity
        });
    }
    if !expected_inventory.is_empty() && expected_inventory.len() != inventory.len() {
        return Err(ToolsUpdateError::Write {
            message: "tools inventory contains an unexpected file".to_owned(),
        });
    }
    let staged_sources =
        slot_tools::staged(staged.path()).map_err(|error| ToolsUpdateError::Write {
            message: error.to_string(),
        })?;
    let files = staged_sources
        .iter()
        .map(|path| {
            path.file_name()
                .and_then(|name| name.to_str())
                .map(str::to_owned)
                .ok_or_else(|| ToolsUpdateError::SourceName { path: path.clone() })
        })
        .collect::<Result<Vec<_>, _>>()?;
    let destination_paths = slot_tools::destinations(root, &staged_sources);
    let snapshot = snapshot_files(&destination_paths)?;
    let tools_directory = root.join("tools");
    let had_tools_directory = tools_directory.is_dir();
    let result =
        slot_tools::commit(root, &staged_sources).map_err(|error| ToolsUpdateError::Write {
            message: error.to_string(),
        });
    if let Err(error) = result {
        restore_snapshot(&snapshot, &tools_directory, had_tools_directory)?;
        return Err(error);
    }
    Ok(ToolsUpdateReceipt { files, inventory })
}

fn snapshot_files(
    destinations: &[PathBuf],
) -> Result<HashMap<PathBuf, Option<Vec<u8>>>, ToolsUpdateError> {
    destinations
        .iter()
        .map(|path| {
            let value = match crate::file_identity::open_readonly(path) {
                Ok(mut file) => {
                    let mut bytes = Vec::new();
                    file.read_to_end(&mut bytes)
                        .map_err(|error| ToolsUpdateError::Snapshot {
                            path: path.clone(),
                            message: error.to_string(),
                        })?;
                    Some(bytes)
                }
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
                Err(error) => {
                    return Err(ToolsUpdateError::Snapshot {
                        path: path.clone(),
                        message: error.to_string(),
                    });
                }
            };
            Ok((path.clone(), value))
        })
        .collect()
}
fn restore_snapshot(
    snapshot: &HashMap<PathBuf, Option<Vec<u8>>>,
    tools_directory: &Path,
    had_tools_directory: bool,
) -> Result<(), ToolsUpdateError> {
    for (path, value) in snapshot {
        match value {
            Some(bytes) => restore_file(path, bytes)?,
            None => match fs::remove_file(path) {
                Ok(()) => {}
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
                Err(error) => {
                    return Err(ToolsUpdateError::Rollback {
                        path: path.clone(),
                        message: error.to_string(),
                    });
                }
            },
        }
    }
    if !had_tools_directory {
        match fs::remove_dir(tools_directory) {
            Ok(()) => {}
            Err(error)
                if error.kind() == std::io::ErrorKind::NotFound
                    || error.kind() == std::io::ErrorKind::DirectoryNotEmpty => {}
            Err(error) => {
                return Err(ToolsUpdateError::Rollback {
                    path: tools_directory.to_owned(),
                    message: error.to_string(),
                });
            }
        }
    }
    Ok(())
}

fn restore_file(path: &Path, bytes: &[u8]) -> Result<(), ToolsUpdateError> {
    crate::slot_storage::write_regular(path, bytes).map_err(|error| ToolsUpdateError::Rollback {
        path: path.to_owned(),
        message: error.to_string(),
    })
}
