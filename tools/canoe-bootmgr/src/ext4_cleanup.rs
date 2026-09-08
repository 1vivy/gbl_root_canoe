//! Complete tree extraction is reserved for explicitly reviewed boot-root removal.
//! Ordinary installer transactions intentionally retain their narrower inventory.
use super::{Ext4Dir, Ext4Error};
use crate::backend::{BackendActionError, BackendError};
use std::{fs, path::Path};
fn backend<E>(error: impl std::fmt::Display) -> BackendActionError<E> {
    BackendActionError::Backend(BackendError::Ext4Typed(Ext4Error::Operation(
        error.to_string(),
    )))
}
impl Ext4Dir {
    pub(crate) fn with_complete_root<T, E>(
        &self,
        write: bool,
        action: impl FnOnce(&Path) -> Result<T, E>,
    ) -> Result<T, BackendActionError<E>> {
        let temporary = tempfile::tempdir().map_err(backend)?;
        let root = temporary.path().join("efisp");
        fs::create_dir(&root).map_err(backend)?;
        self.extract_complete(&root, "", &mut 0).map_err(backend)?;
        let before = temporary.path().join("before");
        if write {
            Self::snapshot_temp_root(&root, &before).map_err(backend)?;
        }
        let result = action(&root).map_err(BackendActionError::Action)?;
        if write {
            self.sync_tree(&root, &before, true).map_err(backend)?;
            // Verify the actual exported filesystem, not just the temporary tree.
            if !self
                .list_directory(&self.remote(""))
                .map_err(backend)?
                .is_empty()
            {
                return Err(backend(
                    "cleanup readback found remaining boot-root entries",
                ));
            }
        }
        Ok(result)
    }
    fn extract_complete(
        &self,
        root: &Path,
        relative: &str,
        count: &mut usize,
    ) -> Result<(), Ext4Error> {
        if relative.split('/').count() > 64 {
            return Err(Ext4Error::Operation(
                "boot root exceeds cleanup depth limit".into(),
            ));
        }
        for entry in self.list_directory(&self.remote(relative))? {
            *count += 1;
            if *count > 10000
                || entry.name.is_empty()
                || entry.name == "."
                || entry.name == ".."
                || entry.name.contains(['/', '\\', '\0'])
            {
                return Err(Ext4Error::Operation("unsafe cleanup inventory".into()));
            }
            let logical = format!("{relative}/{}", entry.name);
            let local = root.join(logical.trim_start_matches('/'));
            match entry.kind.as_str() {
                "directory" => {
                    fs::create_dir(&local).map_err(|e| Ext4Error::Operation(e.to_string()))?;
                    self.extract_complete(root, &logical, count)?;
                }
                "file" => {
                    let bytes = self.read_path(&logical)?.ok_or_else(|| {
                        Ext4Error::Operation("boot root changed during inventory".into())
                    })?;
                    fs::write(local, bytes).map_err(|e| Ext4Error::Operation(e.to_string()))?;
                }
                _ => {
                    return Err(Ext4Error::Operation(
                        "cleanup refuses symbolic links and special files".into(),
                    ));
                }
            }
        }
        Ok(())
    }
}
