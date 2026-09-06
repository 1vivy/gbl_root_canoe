use std::ffi::OsStr;
use std::fs;
use std::path::{Path, PathBuf};

use crate::file_identity::FileIdentity;
use crate::tools_update::ToolsUpdateError;

pub(crate) fn source_files(source: &Path) -> Result<Vec<PathBuf>, ToolsUpdateError> {
    let metadata = fs::symlink_metadata(source).map_err(|error| {
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
        let path = entry.path();
        let metadata = fs::symlink_metadata(&path).map_err(|error| ToolsUpdateError::Snapshot {
            path: path.clone(),
            message: error.to_string(),
        })?;
        if metadata.is_file() {
            files.push(path);
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

/// Resolve one reviewed tool by its portable identity name.
pub(crate) fn expected_for_name<'a>(
    inventory: &'a [FileIdentity],
    name: &OsStr,
) -> Result<Option<&'a FileIdentity>, String> {
    let mut entries = inventory.iter().filter(|entry| {
        entry
            .path
            .file_name()
            .is_some_and(|candidate| candidate == name)
    });
    let expected = entries.next();
    if entries.next().is_some() {
        return Err(format!(
            "tools inventory has duplicate name {}",
            name.to_string_lossy()
        ));
    }
    Ok(expected)
}

pub(crate) fn inventory(source: &Path) -> Result<Vec<FileIdentity>, ToolsUpdateError> {
    source_files(source)?
        .iter()
        .map(|path| {
            let file = crate::file_identity::open_readonly(path).map_err(|error| {
                ToolsUpdateError::Snapshot {
                    path: path.clone(),
                    message: error.to_string(),
                }
            })?;
            crate::file_identity::identity(&file, path).map_err(|error| {
                ToolsUpdateError::Snapshot {
                    path: path.clone(),
                    message: error.to_string(),
                }
            })
        })
        .collect()
}
