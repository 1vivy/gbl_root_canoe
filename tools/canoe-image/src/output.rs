//! Explicit output publication; recovery and readback belong to the application.
use std::{io, path::Path};
pub(crate) fn distinct(output: &Path, inputs: &[&Path]) -> io::Result<()> {
    canoe_fs::file::distinct(output, inputs)
}
pub(crate) fn write(output: &Path, bytes: &[u8]) -> io::Result<()> {
    if let Some(parent) = output.parent().filter(|p| !p.as_os_str().is_empty()) {
        std::fs::create_dir_all(parent)?;
    }
    canoe_fs::file::write(output, bytes)
}
