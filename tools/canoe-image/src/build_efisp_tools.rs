use crate::build::{BuildError, io_error};
use canoe_fs::confined::Root;
use std::{fs, path::Path};
pub(crate) fn stage(source: &Path, staged: &Path) -> Result<usize, BuildError> {
    let input = Root::open(source).map_err(|e| io_error("open EFI tools", source, e))?;
    let files: Vec<_> = input
        .names("")
        .map_err(|e| io_error("list EFI tools", source, e))?
        .into_iter()
        .filter(|n| n.to_ascii_lowercase().ends_with(".efi"))
        .collect();
    if files.len() > 32 {
        return Err(BuildError::Invalid {
            step: "efisp-tools",
            message: "too many EFI tools".into(),
        });
    }
    let destination = staged.join("tools");
    fs::create_dir(&destination)
        .map_err(|e| io_error("create private tools directory", &destination, e))?;
    let output = Root::open(&destination)
        .map_err(|e| io_error("open private tools directory", &destination, e))?;
    for name in &files {
        let bytes = input
            .read(name, 16 * 1024 * 1024)
            .map_err(|e| io_error("read EFI tool", source, e))?;
        output
            .write(name, &bytes, false)
            .map_err(|e| io_error("stage EFI tool", &destination, e))?;
    }
    Ok(files.len())
}
