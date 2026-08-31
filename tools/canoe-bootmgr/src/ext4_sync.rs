use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use super::{Ext4Dir, Ext4Error, KNOWN_FILES, Listed};
use crate::bls::BlsEntry;

impl Ext4Dir {
    pub(super) fn populate_temp(&self, root: &Path) -> Result<(), Ext4Error> {
        for remote in KNOWN_FILES {
            let local = root.join(remote.trim_start_matches('/'));
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
        self.populate_bls(root)?;
        Ok(())
    }

    fn populate_bls(&self, root: &Path) -> Result<(), Ext4Error> {
        let source = self
            .source
            .to_str()
            .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
        let entries_dir = self.remote("/loader/entries");
        let output = Command::new(&self.helper)
            .args(["list", source, entries_dir.as_str()])
            .output()
            .map_err(|error| io("list BLS files", Path::new(&entries_dir), error))?;
        if output.status.code() == Some(7) {
            return Ok(());
        }
        if !output.status.success() {
            return Err(Ext4Error::Operation(
                String::from_utf8_lossy(&output.stderr).trim().to_owned(),
            ));
        }
        let entries: Vec<Listed> = serde_json::from_slice(&output.stdout)
            .map_err(|error| Ext4Error::Output(error.to_string()))?;
        let directory = root.join("loader/entries");
        for entry in entries.into_iter().filter(|entry| entry.kind == "file") {
            if let Some(bytes) = self.read_path(&format!("/loader/entries/{}", entry.name))? {
                if let Ok(parsed) = BlsEntry::parse(&bytes) {
                    self.populate_artifacts(root, &parsed)?;
                }
                fs::write(directory.join(entry.name), bytes)
                    .map_err(|error| io("populate BLS file", &directory, error))?;
            }
        }
        Ok(())
    }

    fn populate_artifacts(&self, root: &Path, entry: &BlsEntry) -> Result<(), Ext4Error> {
        for path in [
            Some(entry.image.as_str()),
            entry.initrd.as_deref(),
            entry.devicetree.as_deref(),
        ]
        .into_iter()
        .flatten()
        {
            let relative = path.replace('\\', "/");
            let relative = relative.trim_start_matches('/');
            if relative.is_empty()
                || relative
                    .split('/')
                    .any(|part| part.is_empty() || part == "." || part == "..")
            {
                continue;
            }
            if let Some(bytes) = self.read_path(&format!("/{relative}"))? {
                let local = root.join(relative);
                if let Some(parent) = local.parent() {
                    fs::create_dir_all(parent)
                        .map_err(|error| io("create artifact directory", parent, error))?;
                }
                fs::write(local, bytes)
                    .map_err(|error| io("populate BLS artifact", root, error))?;
            }
        }
        Ok(())
    }

    pub(super) fn sync_temp(&self, root: &Path) -> Result<(), Ext4Error> {
        for remote in KNOWN_FILES {
            let local = root.join(remote.trim_start_matches('/'));
            if !local.exists() && !remote.ends_with('/') {
                self.remove_path(remote)?;
            }
        }
        sync_tree(self, root, root)?;
        Ok(())
    }
}

fn sync_tree(backend: &Ext4Dir, root: &Path, current: &Path) -> Result<(), Ext4Error> {
    let entries =
        fs::read_dir(current).map_err(|source| io("read temporary directory", current, source))?;
    for item in entries {
        let item = item.map_err(|source| io("read temporary entry", current, source))?;
        let path = item.path();
        let relative = path
            .strip_prefix(root)
            .map_err(|_| Ext4Error::Output("temporary path escaped root".to_owned()))?;
        let remote = format!("/{}", relative.to_string_lossy().replace('\\', "/"));
        if item
            .file_type()
            .map_err(|source| io("stat temporary entry", &path, source))?
            .is_dir()
        {
            let source = backend
                .source
                .to_str()
                .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
            let target = backend.remote(&remote);
            backend.command(&["--recover", "--mkdir-p", "mkdir", source, &target], None)?;
            sync_tree(backend, root, &path)?;
        } else {
            backend.write_path(
                &remote,
                &fs::read(&path).map_err(|source| io("read temporary file", &path, source))?,
            )?;
        }
    }
    Ok(())
}

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> Ext4Error {
    Ext4Error::Io {
        operation,
        path: PathBuf::from(path),
        source,
    }
}
