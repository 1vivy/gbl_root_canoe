use std::path::Path;
use std::process::Command;

use super::{Ext4Dir, Ext4Error, Listed};
use crate::backend::{BackendError, BlsFile, BootRoot};
use crate::bls::BlsEntry;
use crate::config::ConfigDocument;

impl BootRoot for Ext4Dir {
    fn root(&self) -> &Path {
        &self.source
    }

    fn read_config(&self) -> Result<Option<ConfigDocument>, BackendError> {
        self.read_path("/canoe.cfg")
            .map_err(BackendError::Ext4Typed)?
            .map(|bytes| ConfigDocument::parse(&bytes).map_err(BackendError::Config))
            .transpose()
    }

    fn write_config(&self, config: &ConfigDocument) -> Result<(), BackendError> {
        self.write_path("/canoe.cfg", &config.serialize()?)
            .map_err(BackendError::Ext4Typed)
    }

    fn list_bls(&self) -> Result<Vec<BlsFile>, BackendError> {
        let source = self
            .source
            .to_str()
            .ok_or_else(|| BackendError::Ext4("source path is not UTF-8".to_owned()))?;
        let entries_dir = self.remote("/loader/entries");
        self.ensure_remote_components(&entries_dir)
            .map_err(BackendError::Ext4Typed)?;
        let output = Command::new(&self.helper)
            .args(["list", source, entries_dir.as_str()])
            .output()
            .map_err(|source_error| {
                BackendError::Ext4Typed(Ext4Error::Io {
                    operation: "list BLS files",
                    path: self.helper.clone(),
                    source: source_error,
                })
            })?;
        if output.status.code() == Some(7) {
            return Ok(Vec::new());
        }
        if !output.status.success() {
            let message = String::from_utf8_lossy(&output.stderr).trim().to_owned();
            return Err(BackendError::Ext4Typed(Ext4Error::Helper {
                message: if message.is_empty() {
                    format!("helper exited {}", output.status)
                } else {
                    message
                },
            }));
        }
        let names: Vec<Listed> = serde_json::from_slice(&output.stdout)
            .map_err(|error| BackendError::Ext4(error.to_string()))?;
        let mut entries = Vec::new();
        for item in names.into_iter().filter(|item| item.kind == "file") {
            if let Some(bytes) = self
                .read_path(&format!("/loader/entries/{}", item.name))
                .map_err(BackendError::Ext4Typed)?
            {
                if let Ok(entry) = BlsEntry::parse(&bytes) {
                    entries.push(BlsFile {
                        name: item.name,
                        entry,
                    });
                }
            }
        }
        entries.sort_by(|left, right| left.name.cmp(&right.name));
        Ok(entries)
    }

    fn read_bls(&self, name: &str) -> Result<BlsFile, BackendError> {
        if name.is_empty()
            || name.contains(['/', '\\'])
            || !name.to_ascii_lowercase().ends_with(".conf")
        {
            return Err(BackendError::InvalidBlsName(name.to_owned()));
        }
        let path = format!("/loader/entries/{name}");
        let bytes = self
            .read_path(&path)
            .map_err(BackendError::Ext4Typed)?
            .ok_or_else(|| BackendError::Ext4(format!("BLS file not found: {name}")))?;
        Ok(BlsFile {
            name: name.to_owned(),
            entry: BlsEntry::parse(&bytes)?,
        })
    }
}
