use std::fs;
use std::path::{Path, PathBuf};

use super::{Ext4Dir, Ext4Error};

impl Ext4Dir {
    pub(super) fn snapshot_temp_root(root: &Path, expected_root: &Path) -> Result<(), Ext4Error> {
        fs::create_dir(expected_root)
            .map_err(|source| io("create expected temporary root", expected_root, source))?;
        copy_temp_tree(root, expected_root)
    }

    pub(super) fn sync_temp(&self, root: &Path, expected_root: &Path) -> Result<(), Ext4Error> {
        let manifest = super::ext4_delta::sync_manifest(self, root, expected_root)?;
        if manifest.is_empty() {
            return Ok(());
        }
        let manifest_path = expected_root.join(".canoe-ext4-sync.manifest");
        fs::write(&manifest_path, manifest)
            .map_err(|source| io("write ext4 sync manifest", &manifest_path, source))?;
        let source = self
            .source
            .to_str()
            .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
        let manifest = manifest_path
            .to_str()
            .ok_or_else(|| Ext4Error::Output("sync manifest path is not UTF-8".to_owned()))?;
        let desired = root
            .to_str()
            .ok_or_else(|| Ext4Error::Output("sync root path is not UTF-8".to_owned()))?;
        let expected = expected_root
            .to_str()
            .ok_or_else(|| Ext4Error::Output("expected root path is not UTF-8".to_owned()))?;
        self.command(
            &["--recover", "sync", source, manifest, desired, expected],
            None,
        )
        .map(|_| ())
    }
}

fn copy_temp_tree(source: &Path, destination: &Path) -> Result<(), Ext4Error> {
    let entries = fs::read_dir(source)
        .map_err(|error| io("read temporary snapshot directory", source, error))?;
    for entry in entries {
        let entry = entry.map_err(|error| io("read temporary snapshot entry", source, error))?;
        let source_path = entry.path();
        let destination_path = destination.join(entry.file_name());
        let file_type = entry
            .file_type()
            .map_err(|error| io("stat temporary snapshot entry", &source_path, error))?;
        if file_type.is_dir() {
            fs::create_dir(&destination_path).map_err(|error| {
                io(
                    "create expected temporary directory",
                    &destination_path,
                    error,
                )
            })?;
            copy_temp_tree(&source_path, &destination_path)?;
        } else if file_type.is_file() {
            fs::copy(&source_path, &destination_path)
                .map_err(|error| io("copy expected temporary file", &source_path, error))?;
        } else {
            return Err(Ext4Error::Operation(format!(
                "unsupported temporary ext4 entry type: {}",
                source_path.display()
            )));
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
