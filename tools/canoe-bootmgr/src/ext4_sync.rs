use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use super::{Ext4Dir, Ext4Error, KNOWN_FILES, Listed};
use crate::bls::BlsEntry;

impl Ext4Dir {
    pub(super) fn populate_temp(&self, root: &Path) -> Result<(), Ext4Error> {
        for remote in KNOWN_FILES {
            let relative = remote.strip_prefix('/').ok_or_else(|| {
                Ext4Error::Operation(format!("invalid known ext4 path: {remote:?}"))
            })?;
            let local = temp_destination(root, relative)?;
            if remote == "/loader/entries" || remote == "/.canoe-quarantine" {
                fs::create_dir_all(&local)
                    .map_err(|source| io("create temporary directory", &local, source))?;
                continue;
            }
            if let Some(bytes) = self.read_path(remote)? {
                if let Some(parent) = local.parent() {
                    fs::create_dir_all(parent)
                        .map_err(|source| io("create temporary parent", parent, source))?;
                }
                fs::write(&local, bytes)
                    .map_err(|source| io("populate temporary file", &local, source))?;
            }
        }
        self.populate_config_artifacts(root)?;
        self.populate_bls(root)?;
        self.populate_tools(root)?;
        Ok(())
    }

    fn populate_config_artifacts(&self, root: &Path) -> Result<(), Ext4Error> {
        let Some(bytes) = self.read_path("/canoe.cfg")? else {
            return Ok(());
        };
        let config = crate::config::ConfigDocument::parse(&bytes)
            .map_err(|error| Ext4Error::Operation(error.to_string()))?;
        for entry in config.entries {
            let paths = ["", ".gm2p", ".tzmap"].map(|suffix| format!("{}{}", entry.image, suffix));
            let destinations = paths
                .iter()
                .map(|path| temp_destination(root, path).map(|local| (path, local)))
                .collect::<Result<Vec<_>, Ext4Error>>()?;
            for (path, local) in destinations {
                let Some(bytes) = self.read_path(&format!("/{path}"))? else {
                    continue;
                };
                if let Some(parent) = local.parent() {
                    fs::create_dir_all(parent)
                        .map_err(|source| io("create config artifact directory", parent, source))?;
                }
                fs::write(&local, bytes)
                    .map_err(|source| io("populate config artifact", &local, source))?;
            }
        }
        Ok(())
    }

    fn populate_tools(&self, root: &Path) -> Result<(), Ext4Error> {
        let remote = self.remote("/tools");
        self.ensure_remote_components(&remote)?;
        let entries = self.list_directory(&remote)?;
        let directory = root.join("tools");
        fs::create_dir_all(&directory)
            .map_err(|source| io("create temporary tools directory", &directory, source))?;
        for entry in entries.into_iter().filter(|entry| entry.kind == "file") {
            let relative = format!("tools/{}", parse_file_name(&entry.name)?);
            let local = temp_destination(root, &relative)?;
            if local.is_file() {
                continue;
            }
            if let Some(bytes) = self.read_path(&format!("/{relative}"))? {
                fs::write(&local, bytes)
                    .map_err(|source| io("populate tool file", &local, source))?;
            }
        }
        Ok(())
    }

    fn populate_bls(&self, root: &Path) -> Result<(), Ext4Error> {
        let source = self
            .source
            .to_str()
            .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
        let entries_dir = self.remote("/loader/entries");
        self.ensure_remote_components(&entries_dir)?;
        let output = Command::new(&self.helper)
            .args(["list", source, entries_dir.as_str()])
            .output()
            .map_err(|error| io("list BLS files", Path::new(&entries_dir), error))?;
        if output.status.code() == Some(7) {
            return Ok(());
        }
        if !output.status.success() {
            let message = String::from_utf8_lossy(&output.stderr).trim().to_owned();
            return Err(Ext4Error::Helper {
                message: if message.is_empty() {
                    format!("helper exited {}", output.status)
                } else {
                    message
                },
            });
        }
        let entries: Vec<Listed> = serde_json::from_slice(&output.stdout)
            .map_err(|error| Ext4Error::Output(error.to_string()))?;
        let entries = entries
            .into_iter()
            .filter(|entry| entry.kind == "file")
            .map(|entry| parse_file_name(&entry.name).map(str::to_owned))
            .collect::<Result<Vec<_>, Ext4Error>>()?;
        for name in entries {
            let remote = format!("/loader/entries/{name}");
            let local = temp_destination(root, &format!("loader/entries/{name}"))?;
            if let Some(bytes) = self.read_path(&remote)? {
                if let Ok(parsed) = BlsEntry::parse(&bytes) {
                    self.populate_artifacts(root, &parsed)?;
                }
                fs::write(&local, bytes).map_err(|error| io("populate BLS file", &local, error))?;
            }
        }
        Ok(())
    }

    fn populate_artifacts(&self, root: &Path, entry: &BlsEntry) -> Result<(), Ext4Error> {
        let artifacts = [
            Some(entry.image.as_str()),
            entry.initrd.as_deref(),
            entry.devicetree.as_deref(),
        ]
        .into_iter()
        .flatten()
        .map(|path| bls_artifact_destination(root, path))
        .collect::<Result<Vec<_>, Ext4Error>>()?;
        for (relative, local) in artifacts {
            if let Some(bytes) = self.read_path(&format!("/{relative}"))? {
                if let Some(parent) = local.parent() {
                    fs::create_dir_all(parent)
                        .map_err(|error| io("create artifact directory", parent, error))?;
                }
                fs::write(&local, bytes)
                    .map_err(|error| io("populate BLS artifact", &local, error))?;
            }
        }
        Ok(())
    }
}

#[derive(Debug, PartialEq, Eq)]
struct RelativePath<'a> {
    components: Vec<&'a str>,
}

fn parse_relative_path(logical: &str) -> Result<RelativePath<'_>, Ext4Error> {
    let components = logical.split('/').collect::<Vec<_>>();
    if logical.is_empty()
        || logical.starts_with('/')
        || logical.contains('\\')
        || logical.contains(':')
        || components
            .iter()
            .any(|component| component.is_empty() || matches!(*component, "." | ".."))
    {
        return Err(unsafe_relative_path(logical));
    }
    Ok(RelativePath { components })
}

fn parse_file_name(logical: &str) -> Result<&str, Ext4Error> {
    let path = parse_relative_path(logical)?;
    match path.components.as_slice() {
        [component] => Ok(*component),
        _ => Err(unsafe_relative_path(logical)),
    }
}

fn temp_destination(root: &Path, logical: &str) -> Result<PathBuf, Ext4Error> {
    let relative = parse_relative_path(logical)?;
    Ok(relative
        .components
        .into_iter()
        .fold(root.to_path_buf(), |mut destination, component| {
            destination.push(component);
            destination
        }))
}

fn bls_artifact_destination(root: &Path, logical: &str) -> Result<(String, PathBuf), Ext4Error> {
    let folded = logical.replace('\\', "/");
    let relative = folded.strip_prefix('/').unwrap_or(&folded);
    let local = temp_destination(root, relative).map_err(|_| unsafe_relative_path(logical))?;
    Ok((relative.to_owned(), local))
}

fn unsafe_relative_path(logical: &str) -> Ext4Error {
    Ext4Error::Operation(format!("unsafe ext4 relative path: {logical:?}"))
}

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> Ext4Error {
    Ext4Error::Io {
        operation,
        path: PathBuf::from(path),
        source,
    }
}

#[cfg(test)]
#[path = "ext4_sync_tests.rs"]
mod tests;
