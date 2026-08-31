use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use crate::build::{io_error, BuildError};

struct AuxSnapshot {
    path: PathBuf,
    original: Option<Vec<u8>>,
}

pub(crate) struct Cleanup {
    staged: PathBuf,
    aux: Vec<AuxSnapshot>,
    committed: bool,
}

impl Cleanup {
    pub(crate) fn prepare(
        staged: &Path,
        keep: Option<&Path>,
        patch_log: Option<&Path>,
    ) -> Result<Self, BuildError> {
        fs::create_dir_all(staged)
            .map_err(|source| io_error("create staged directory", staged, source))?;
        for name in ["boot.efi", "boot.efi.gm2p", "boot.efi.tzmap"] {
            let path = staged.join(name);
            match fs::remove_file(&path) {
                Ok(()) => {}
                Err(error) if error.kind() == io::ErrorKind::NotFound => {}
                Err(source) => return Err(io_error("remove stale staged output", &path, source)),
            }
        }
        let mut aux = Vec::new();
        for path in [keep, patch_log].into_iter().flatten() {
            let original = match fs::read(path) {
                Ok(bytes) => Some(bytes),
                Err(error) if error.kind() == io::ErrorKind::NotFound => None,
                Err(source) => return Err(io_error("snapshot auxiliary output", path, source)),
            };
            aux.push(AuxSnapshot { path: path.to_owned(), original });
        }
        Ok(Self { staged: staged.to_owned(), aux, committed: false })
    }

    pub(crate) fn rollback(&self) {
        for name in ["boot.efi", "boot.efi.gm2p", "boot.efi.tzmap"] {
            let _ = fs::remove_file(self.staged.join(name));
        }
        for snapshot in &self.aux {
            match &snapshot.original {
                Some(bytes) => {
                    let _ = fs::write(&snapshot.path, bytes);
                }
                None => {
                    let _ = fs::remove_file(&snapshot.path);
                }
            }
        }
    }

    pub(crate) fn commit(&mut self) {
        self.committed = true;
    }
}

impl Drop for Cleanup {
    fn drop(&mut self) {
        if !self.committed {
            self.rollback();
        }
    }
}

pub(crate) fn ensure_parent(path: &Path, step: &'static str) -> Result<(), BuildError> {
    if let Some(parent) = path.parent().filter(|parent| !parent.as_os_str().is_empty()) {
        fs::create_dir_all(parent).map_err(|source| io_error(step, parent, source))?;
    }
    Ok(())
}

pub(crate) fn copy_aux(source: &Path, destination: &Path, step: &'static str) -> Result<(), BuildError> {
    ensure_parent(destination, step)?;
    fs::copy(source, destination)
        .map(|_| ())
        .map_err(|source| io_error(step, destination, source))
}

pub(crate) fn write_aux(path: &Path, bytes: &[u8], step: &'static str) -> Result<(), BuildError> {
    ensure_parent(path, step)?;
    fs::write(path, bytes).map_err(|source| io_error(step, path, source))
}
