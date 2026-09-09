//! Publication of explicitly requested auxiliary files. No snapshot, recursive
//! deletion or rollback over caller-owned paths belongs in an image command.
use crate::build::{BuildError, io_error};
use std::{fs, path::Path};
pub(crate) fn ensure_parent(path: &Path, step: &'static str) -> Result<(), BuildError> {
    if let Some(parent) = path.parent().filter(|p| !p.as_os_str().is_empty()) {
        fs::create_dir_all(parent).map_err(|e| io_error(step, path, e))?;
    }
    Ok(())
}
pub(crate) fn copy_aux(
    source: &Path,
    destination: &Path,
    step: &'static str,
) -> Result<(), BuildError> {
    let bytes =
        canoe_fs::file::read(source, 16 * 1024 * 1024).map_err(|e| io_error(step, source, e))?;
    write_aux(destination, &bytes, step)
}
pub(crate) fn write_aux(path: &Path, bytes: &[u8], step: &'static str) -> Result<(), BuildError> {
    ensure_parent(path, step)?;
    canoe_fs::file::write(path, bytes).map_err(|e| io_error(step, path, e))
}
