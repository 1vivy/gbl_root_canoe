use std::ffi::OsString;
use std::fs;
use std::path::Path;

use crate::build::{BuildError, io_error};

pub(crate) fn stage(source: &Path, staged: &Path) -> Result<usize, BuildError> {
    if !source.is_dir() {
        return Err(BuildError::Invalid {
            step: "efisp-tools",
            message: format!("{} is not an existing directory", source.display()),
        });
    }

    let mut files = Vec::new();
    for entry in
        fs::read_dir(source).map_err(|error| io_error("read efisp-tools", source, error))?
    {
        let entry = entry.map_err(|error| io_error("read efisp-tools", source, error))?;
        if entry
            .file_type()
            .map_err(|error| io_error("inspect efisp-tools entry", &entry.path(), error))?
            .is_file()
        {
            files.push((entry.file_name(), entry.path()));
        }
    }
    files.sort_by(|(left, _), (right, _)| compare_names(left, right));

    let destination = staged.join("tools");
    fs::create_dir_all(&destination)
        .map_err(|error| io_error("create staged tools directory", &destination, error))?;
    for (name, file) in &files {
        let output = destination.join(name);
        fs::copy(file, &output).map_err(|error| io_error("stage efisp tool", &output, error))?;
    }
    Ok(files.len())
}

fn compare_names(left: &OsString, right: &OsString) -> std::cmp::Ordering {
    left.cmp(right)
}
