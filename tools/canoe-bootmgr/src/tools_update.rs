use std::collections::HashMap;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

use thiserror::Error;

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

pub(crate) fn update(root: &Path, source: &Path) -> Result<Vec<String>, ToolsUpdateError> {
    let sources = source_files(source)?;
    let files = sources
        .iter()
        .map(|path| {
            path.file_name()
                .and_then(|name| name.to_str())
                .map(str::to_owned)
                .ok_or_else(|| ToolsUpdateError::SourceName { path: path.clone() })
        })
        .collect::<Result<Vec<_>, _>>()?;
    let destination_paths = slot_tools::destinations(root, &sources);
    let snapshot = snapshot_files(&destination_paths)?;
    let tools_directory = root.join("tools");
    let had_tools_directory = tools_directory.is_dir();
    let result = slot_tools::commit(root, &sources).map_err(|error| ToolsUpdateError::Write {
        message: error.to_string(),
    });
    if let Err(error) = result {
        if let Err(rollback) = restore_snapshot(&snapshot, &tools_directory, had_tools_directory) {
            return Err(rollback);
        }
        return Err(error);
    }
    Ok(files)
}

fn source_files(source: &Path) -> Result<Vec<PathBuf>, ToolsUpdateError> {
    let metadata = fs::metadata(source).map_err(|error| {
        if error.kind() == std::io::ErrorKind::NotFound {
            ToolsUpdateError::SourceMissing {
                source_path: source.to_owned(),
            }
        } else {
            ToolsUpdateError::Snapshot {
                path: source.to_owned(),
                message: error.to_string(),
            }
        }
    })?;
    if !metadata.is_dir() {
        return Err(ToolsUpdateError::SourceNotDirectory {
            source_path: source.to_owned(),
        });
    }
    let entries = fs::read_dir(source).map_err(|error| ToolsUpdateError::Snapshot {
        path: source.to_owned(),
        message: error.to_string(),
    })?;
    let mut files = Vec::new();
    for entry in entries {
        let entry = entry.map_err(|error| ToolsUpdateError::Snapshot {
            path: source.to_owned(),
            message: error.to_string(),
        })?;
        if entry.path().is_file() {
            files.push(entry.path());
        }
    }
    files.sort();
    if files.is_empty() {
        return Err(ToolsUpdateError::SourceEmpty {
            source_path: source.to_owned(),
        });
    }
    Ok(files)
}

fn snapshot_files(
    destinations: &[PathBuf],
) -> Result<HashMap<PathBuf, Option<Vec<u8>>>, ToolsUpdateError> {
    destinations
        .iter()
        .map(|path| {
            let value = match fs::read(path) {
                Ok(bytes) => Some(bytes),
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
            Some(bytes) => {
                let current = fs::read(path);
                if current.as_ref().is_ok_and(|current| current == bytes) {
                    continue;
                }
                restore_file(path, bytes)?;
            }
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
    let mut file = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(path)
        .map_err(|error| ToolsUpdateError::Rollback {
            path: path.to_owned(),
            message: error.to_string(),
        })?;
    file.write_all(bytes).map_err(|error| ToolsUpdateError::Rollback {
        path: path.to_owned(),
        message: error.to_string(),
    })?;
    file.sync_all().map_err(|error| ToolsUpdateError::Rollback {
        path: path.to_owned(),
        message: error.to_string(),
    })
}
